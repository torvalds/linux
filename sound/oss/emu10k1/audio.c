/*
 **********************************************************************
 *     audio.c -- /dev/dsp interface for emu10k1 driver
 *     Copyright 1999, 2000 Creative Labs, Inc.
 *
 **********************************************************************
 *
 *     Date                 Author          Summary of changes
 *     ----                 ------          ------------------
 *     October 20, 1999     Bertrand Lee    base code release
 *     November 2, 1999	    Alan Cox        cleaned up types/leaks
 *
 **********************************************************************
 *
 *     This program is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License as
 *     published by the Free Software Foundation; either version 2 of
 *     the License, or (at your option) any later version.
 *
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public
 *     License along with this program; if not, write to the Free
 *     Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139,
 *     USA.
 *
 **********************************************************************
 */

#include <linux/module.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/bitops.h>
#include <asm/io.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp_lock.h>

#include "hwaccess.h"
#include "cardwo.h"
#include "cardwi.h"
#include "recmgr.h"
#include "irqmgr.h"
#include "audio.h"
#include "8010.h"

static void calculate_ofrag(struct woinst *);
static void calculate_ifrag(struct wiinst *);

static void emu10k1_waveout_bh(unsigned long refdata);
static void emu10k1_wavein_bh(unsigned long refdata);

/* Audio file operations */
static ssize_t emu10k1_audio_read(struct file *file, char __user *buffer, size_t count, loff_t * ppos)
{
	struct emu10k1_wavedevice *wave_dev = (struct emu10k1_wavedevice *) file->private_data;
	struct wiinst *wiinst = wave_dev->wiinst;
	ssize_t ret = 0;
	unsigned long flags;

	DPD(3, "emu10k1_audio_read(), buffer=%p, count=%d\n", buffer, (u32) count);

	if (!access_ok(VERIFY_WRITE, buffer, count))
		return -EFAULT;

	spin_lock_irqsave(&wiinst->lock, flags);

	if (wiinst->mmapped) {
		spin_unlock_irqrestore(&wiinst->lock, flags);
		return -ENXIO;
	}

	if (wiinst->state == WAVE_STATE_CLOSED) {
		calculate_ifrag(wiinst);

		while (emu10k1_wavein_open(wave_dev) < 0) {
			spin_unlock_irqrestore(&wiinst->lock, flags);

			if (file->f_flags & O_NONBLOCK)
				return -EAGAIN;

			interruptible_sleep_on(&wave_dev->card->open_wait);

			if (signal_pending(current))
				return -ERESTARTSYS;

			spin_lock_irqsave(&wiinst->lock, flags);
		}
	}

	spin_unlock_irqrestore(&wiinst->lock, flags);

	while (count > 0) {
		u32 bytestocopy;

		spin_lock_irqsave(&wiinst->lock, flags);

		if (!(wiinst->state & WAVE_STATE_STARTED)
		    && (wave_dev->enablebits & PCM_ENABLE_INPUT))
			emu10k1_wavein_start(wave_dev);

		emu10k1_wavein_update(wave_dev->card, wiinst);
		emu10k1_wavein_getxfersize(wiinst, &bytestocopy);

		spin_unlock_irqrestore(&wiinst->lock, flags);

		DPD(3, "bytestocopy --> %d\n", bytestocopy);

		if ((bytestocopy >= wiinst->buffer.fragment_size)
		    || (bytestocopy >= count)) {
			int rc;

			bytestocopy = min_t(u32, bytestocopy, count);

			rc = emu10k1_wavein_xferdata(wiinst,
						     (u8 __user *)buffer,
						     &bytestocopy);
			if (rc)
				return rc;

			count -= bytestocopy;
			buffer += bytestocopy;
			ret += bytestocopy;
		}

		if (count > 0) {
			if ((file->f_flags & O_NONBLOCK)
			    || (!(wave_dev->enablebits & PCM_ENABLE_INPUT)))
				return (ret ? ret : -EAGAIN);

			interruptible_sleep_on(&wiinst->wait_queue);

			if (signal_pending(current))
				return (ret ? ret : -ERESTARTSYS);

		}
	}

	DPD(3, "bytes copied -> %d\n", (u32) ret);

	return ret;
}

static ssize_t emu10k1_audio_write(struct file *file, const char __user *buffer, size_t count, loff_t * ppos)
{
	struct emu10k1_wavedevice *wave_dev = (struct emu10k1_wavedevice *) file->private_data;
	struct woinst *woinst = wave_dev->woinst;
	ssize_t ret;
	unsigned long flags;

	DPD(3, "emu10k1_audio_write(), buffer=%p, count=%d\n", buffer, (u32) count);

	if (!access_ok(VERIFY_READ, buffer, count))
		return -EFAULT;

	spin_lock_irqsave(&woinst->lock, flags);

	if (woinst->mmapped) {
		spin_unlock_irqrestore(&woinst->lock, flags);
		return -ENXIO;
	}
	// This is for emu10k1 revs less than 7, we need to go through tram
	if (woinst->format.passthrough == 1) {
		int r;
		
		woinst->buffer.ossfragshift = PT_BLOCKSIZE_LOG2;
		woinst->buffer.numfrags = PT_BLOCKCOUNT;
		calculate_ofrag(woinst);
		
		r = emu10k1_pt_write(file, buffer, count);
		spin_unlock_irqrestore(&woinst->lock, flags);
		return r;
	}

	if (woinst->state == WAVE_STATE_CLOSED) {
		calculate_ofrag(woinst);

		while (emu10k1_waveout_open(wave_dev) < 0) {
			spin_unlock_irqrestore(&woinst->lock, flags);

			if (file->f_flags & O_NONBLOCK)
				return -EAGAIN;

			interruptible_sleep_on(&wave_dev->card->open_wait);

			if (signal_pending(current))
				return -ERESTARTSYS;

			spin_lock_irqsave(&woinst->lock, flags);
		}
	}

	spin_unlock_irqrestore(&woinst->lock, flags);

	ret = 0;
	if (count % woinst->format.bytespersample)
                return -EINVAL;

	count /= woinst->num_voices;

	while (count > 0) {
		u32 bytestocopy;

		spin_lock_irqsave(&woinst->lock, flags);
		emu10k1_waveout_update(woinst);
		emu10k1_waveout_getxfersize(woinst, &bytestocopy);
		spin_unlock_irqrestore(&woinst->lock, flags);

		DPD(3, "bytestocopy --> %d\n", bytestocopy);

		if ((bytestocopy >= woinst->buffer.fragment_size)
		    || (bytestocopy >= count)) {

			bytestocopy = min_t(u32, bytestocopy, count);

			emu10k1_waveout_xferdata(woinst, (u8 __user *) buffer, &bytestocopy);

			count -= bytestocopy;
			buffer += bytestocopy * woinst->num_voices;
			ret += bytestocopy * woinst->num_voices;

			spin_lock_irqsave(&woinst->lock, flags);
			woinst->total_copied += bytestocopy;

			if (!(woinst->state & WAVE_STATE_STARTED)
			    && (wave_dev->enablebits & PCM_ENABLE_OUTPUT)
			    && (woinst->total_copied >= woinst->buffer.fragment_size))
				emu10k1_waveout_start(wave_dev);

			spin_unlock_irqrestore(&woinst->lock, flags);
		}

		if (count > 0) {
			if ((file->f_flags & O_NONBLOCK)
			    || (!(wave_dev->enablebits & PCM_ENABLE_OUTPUT)))
				return (ret ? ret : -EAGAIN);

			interruptible_sleep_on(&woinst->wait_queue);

			if (signal_pending(current))
				return (ret ? ret : -ERESTARTSYS);
		}
	}

	DPD(3, "bytes copied -> %d\n", (u32) ret);

	return ret;
}

static int emu10k1_audio_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	struct emu10k1_wavedevice *wave_dev = (struct emu10k1_wavedevice *) file->private_data;
	struct woinst *woinst = NULL;
	struct wiinst *wiinst = NULL;
	int val = 0;
	u32 bytestocopy;
	unsigned long flags;
	int __user *p = (int __user *)arg;

	DPF(4, "emu10k1_audio_ioctl()\n");

	if (file->f_mode & FMODE_WRITE)
		woinst = wave_dev->woinst;

	if (file->f_mode & FMODE_READ)
		wiinst = wave_dev->wiinst;

	switch (cmd) {
	case OSS_GETVERSION:
		DPF(2, "OSS_GETVERSION:\n");
		return put_user(SOUND_VERSION, p);

	case SNDCTL_DSP_RESET:
		DPF(2, "SNDCTL_DSP_RESET:\n");
		wave_dev->enablebits = PCM_ENABLE_OUTPUT | PCM_ENABLE_INPUT;

		if (file->f_mode & FMODE_WRITE) {
			spin_lock_irqsave(&woinst->lock, flags);

			if (woinst->state & WAVE_STATE_OPEN) {
				emu10k1_waveout_close(wave_dev);
			}

			woinst->mmapped = 0;
			woinst->total_copied = 0;
			woinst->total_played = 0;
			woinst->blocks = 0;

			spin_unlock_irqrestore(&woinst->lock, flags);
		}

		if (file->f_mode & FMODE_READ) {
			spin_lock_irqsave(&wiinst->lock, flags);

			if (wiinst->state & WAVE_STATE_OPEN) {
				emu10k1_wavein_close(wave_dev);
			}

			wiinst->mmapped = 0;
			wiinst->total_recorded = 0;
			wiinst->blocks = 0;
			spin_unlock_irqrestore(&wiinst->lock, flags);
		}

		break;

	case SNDCTL_DSP_SYNC:
		DPF(2, "SNDCTL_DSP_SYNC:\n");

		if (file->f_mode & FMODE_WRITE) {

			spin_lock_irqsave(&woinst->lock, flags);

			if (woinst->state & WAVE_STATE_OPEN) {

				if (woinst->state & WAVE_STATE_STARTED)
					while ((woinst->total_played < woinst->total_copied)
					       && !signal_pending(current)) {
						spin_unlock_irqrestore(&woinst->lock, flags);
						interruptible_sleep_on(&woinst->wait_queue);
						spin_lock_irqsave(&woinst->lock, flags);
					}
				emu10k1_waveout_close(wave_dev);
			}

			woinst->mmapped = 0;
			woinst->total_copied = 0;
			woinst->total_played = 0;
			woinst->blocks = 0;

			spin_unlock_irqrestore(&woinst->lock, flags);
		}

		if (file->f_mode & FMODE_READ) {
			spin_lock_irqsave(&wiinst->lock, flags);

			if (wiinst->state & WAVE_STATE_OPEN) {
				emu10k1_wavein_close(wave_dev);
			}

			wiinst->mmapped = 0;
			wiinst->total_recorded = 0;
			wiinst->blocks = 0;
			spin_unlock_irqrestore(&wiinst->lock, flags);
		}

		break;

	case SNDCTL_DSP_SETDUPLEX:
		DPF(2, "SNDCTL_DSP_SETDUPLEX:\n");
		break;

	case SNDCTL_DSP_GETCAPS:
		DPF(2, "SNDCTL_DSP_GETCAPS:\n");
		return put_user(DSP_CAP_DUPLEX | DSP_CAP_REALTIME |
				DSP_CAP_TRIGGER | DSP_CAP_MMAP |
				DSP_CAP_COPROC| DSP_CAP_MULTI, p);
	case SNDCTL_DSP_SPEED:
		DPF(2, "SNDCTL_DSP_SPEED:\n");

		if (get_user(val, p))
			return -EFAULT;

		DPD(2, "val is %d\n", val);

		if (val > 0) {
			if (file->f_mode & FMODE_READ) {
				struct wave_format format;

				spin_lock_irqsave(&wiinst->lock, flags);

				format = wiinst->format;
				format.samplingrate = val;

				if (emu10k1_wavein_setformat(wave_dev, &format) < 0) {
					spin_unlock_irqrestore(&wiinst->lock, flags);
					return -EINVAL;
				}

				val = wiinst->format.samplingrate;

				spin_unlock_irqrestore(&wiinst->lock, flags);

				DPD(2, "set recording sampling rate -> %d\n", val);
			}

			if (file->f_mode & FMODE_WRITE) {
				struct wave_format format;

				spin_lock_irqsave(&woinst->lock, flags);

				format = woinst->format;
				format.samplingrate = val;

				if (emu10k1_waveout_setformat(wave_dev, &format) < 0) {
					spin_unlock_irqrestore(&woinst->lock, flags);
					return -EINVAL;
				}

				val = woinst->format.samplingrate;

				spin_unlock_irqrestore(&woinst->lock, flags);

				DPD(2, "set playback sampling rate -> %d\n", val);
			}

			return put_user(val, p);
		} else {
			if (file->f_mode & FMODE_READ)
				val = wiinst->format.samplingrate;
			else if (file->f_mode & FMODE_WRITE)
				val = woinst->format.samplingrate;

			return put_user(val, p);
		}
		break;

	case SNDCTL_DSP_STEREO:
		DPF(2, "SNDCTL_DSP_STEREO:\n");

		if (get_user(val, p))
			return -EFAULT;

		DPD(2, " val is %d\n", val);

		if (file->f_mode & FMODE_READ) {
			struct wave_format format;

			spin_lock_irqsave(&wiinst->lock, flags);

			format = wiinst->format;
			format.channels = val ? 2 : 1;

			if (emu10k1_wavein_setformat(wave_dev, &format) < 0) {
				spin_unlock_irqrestore(&wiinst->lock, flags);
				return -EINVAL;
			}

			val = wiinst->format.channels - 1;

			spin_unlock_irqrestore(&wiinst->lock, flags);
			DPD(2, "set recording stereo -> %d\n", val);
		}

		if (file->f_mode & FMODE_WRITE) {
			struct wave_format format;

			spin_lock_irqsave(&woinst->lock, flags);

			format = woinst->format;
			format.channels = val ? 2 : 1;

			if (emu10k1_waveout_setformat(wave_dev, &format) < 0) {
				spin_unlock_irqrestore(&woinst->lock, flags);
				return -EINVAL;
			}

			val = woinst->format.channels - 1;

			spin_unlock_irqrestore(&woinst->lock, flags);

			DPD(2, "set playback stereo -> %d\n", val);
		}

		return put_user(val, p);

		break;

	case SNDCTL_DSP_CHANNELS:
		DPF(2, "SNDCTL_DSP_CHANNELS:\n");

		if (get_user(val, p))
			return -EFAULT;

		DPD(2, " val is %d\n", val);

		if (val > 0) {
			if (file->f_mode & FMODE_READ) {
				struct wave_format format;

				spin_lock_irqsave(&wiinst->lock, flags);

				format = wiinst->format;
				format.channels = val;

				if (emu10k1_wavein_setformat(wave_dev, &format) < 0) {
					spin_unlock_irqrestore(&wiinst->lock, flags);
					return -EINVAL;
				}
				val = wiinst->format.channels;

				spin_unlock_irqrestore(&wiinst->lock, flags);
				DPD(2, "set recording number of channels -> %d\n", val);
			}

			if (file->f_mode & FMODE_WRITE) {
				struct wave_format format;

				spin_lock_irqsave(&woinst->lock, flags);

				format = woinst->format;
				format.channels = val;

				if (emu10k1_waveout_setformat(wave_dev, &format) < 0) {
					spin_unlock_irqrestore(&woinst->lock, flags);
					return -EINVAL;
				}

				val = woinst->format.channels;

				spin_unlock_irqrestore(&woinst->lock, flags);
				DPD(2, "set playback number of channels -> %d\n", val);
			}

			return put_user(val, p);
		} else {
			if (file->f_mode & FMODE_READ)
				val = wiinst->format.channels;
			else if (file->f_mode & FMODE_WRITE)
				val = woinst->format.channels;

			return put_user(val, p);
		}
		break;

	case SNDCTL_DSP_GETFMTS:
		DPF(2, "SNDCTL_DSP_GETFMTS:\n");

		if (file->f_mode & FMODE_READ)
			val = AFMT_S16_LE;
		else if (file->f_mode & FMODE_WRITE) {
			val = AFMT_S16_LE | AFMT_U8;
			if (emu10k1_find_control_gpr(&wave_dev->card->mgr,
						     wave_dev->card->pt.patch_name, 
						     wave_dev->card->pt.enable_gpr_name) >= 0)
				val |= AFMT_AC3;
		}
		return put_user(val, p);

	case SNDCTL_DSP_SETFMT:	/* Same as SNDCTL_DSP_SAMPLESIZE */
		DPF(2, "SNDCTL_DSP_SETFMT:\n");

		if (get_user(val, p))
			return -EFAULT;

		DPD(2, " val is %d\n", val);

		if (val != AFMT_QUERY) {
			if (file->f_mode & FMODE_READ) {
				struct wave_format format;

				spin_lock_irqsave(&wiinst->lock, flags);

				format = wiinst->format;
				format.id = val;

				if (emu10k1_wavein_setformat(wave_dev, &format) < 0) {
					spin_unlock_irqrestore(&wiinst->lock, flags);
					return -EINVAL;
				}

				val = wiinst->format.id;

				spin_unlock_irqrestore(&wiinst->lock, flags);
				DPD(2, "set recording format -> %d\n", val);
			}

			if (file->f_mode & FMODE_WRITE) {
				struct wave_format format;

				spin_lock_irqsave(&woinst->lock, flags);

				format = woinst->format;
				format.id = val;

				if (emu10k1_waveout_setformat(wave_dev, &format) < 0) {
					spin_unlock_irqrestore(&woinst->lock, flags);
					return -EINVAL;
				}

				val = woinst->format.id;

				spin_unlock_irqrestore(&woinst->lock, flags);
				DPD(2, "set playback format -> %d\n", val);
			}

			return put_user(val, p);
		} else {
			if (file->f_mode & FMODE_READ)
				val = wiinst->format.id;
			else if (file->f_mode & FMODE_WRITE)
				val = woinst->format.id;

			return put_user(val, p);
		}
		break;

	case SOUND_PCM_READ_BITS:

		if (file->f_mode & FMODE_READ)
			val = wiinst->format.bitsperchannel;
		else if (file->f_mode & FMODE_WRITE)
			val = woinst->format.bitsperchannel;

		return put_user(val, p);

	case SOUND_PCM_READ_RATE:

		if (file->f_mode & FMODE_READ)
			val = wiinst->format.samplingrate;
		else if (file->f_mode & FMODE_WRITE)
			val = woinst->format.samplingrate;

		return put_user(val, p);

	case SOUND_PCM_READ_CHANNELS:

		if (file->f_mode & FMODE_READ)
			val = wiinst->format.channels;
		else if (file->f_mode & FMODE_WRITE)
			val = woinst->format.channels;

		return put_user(val, p);

	case SOUND_PCM_WRITE_FILTER:
		DPF(2, "SOUND_PCM_WRITE_FILTER: not implemented\n");
		break;

	case SOUND_PCM_READ_FILTER:
		DPF(2, "SOUND_PCM_READ_FILTER: not implemented\n");
		break;

	case SNDCTL_DSP_SETSYNCRO:
		DPF(2, "SNDCTL_DSP_SETSYNCRO: not implemented\n");
		break;

	case SNDCTL_DSP_GETTRIGGER:
		DPF(2, "SNDCTL_DSP_GETTRIGGER:\n");

		if (file->f_mode & FMODE_WRITE && (wave_dev->enablebits & PCM_ENABLE_OUTPUT))
			val |= PCM_ENABLE_OUTPUT;

		if (file->f_mode & FMODE_READ && (wave_dev->enablebits & PCM_ENABLE_INPUT))
			val |= PCM_ENABLE_INPUT;

		return put_user(val, p);

	case SNDCTL_DSP_SETTRIGGER:
		DPF(2, "SNDCTL_DSP_SETTRIGGER:\n");

		if (get_user(val, p))
			return -EFAULT;

		if (file->f_mode & FMODE_WRITE) {
			spin_lock_irqsave(&woinst->lock, flags);

			if (val & PCM_ENABLE_OUTPUT) {
				wave_dev->enablebits |= PCM_ENABLE_OUTPUT;
				if (woinst->state & WAVE_STATE_OPEN)
					emu10k1_waveout_start(wave_dev);
			} else {
				wave_dev->enablebits &= ~PCM_ENABLE_OUTPUT;
				if (woinst->state & WAVE_STATE_STARTED)
					emu10k1_waveout_stop(wave_dev);
			}

			spin_unlock_irqrestore(&woinst->lock, flags);
		}

		if (file->f_mode & FMODE_READ) {
			spin_lock_irqsave(&wiinst->lock, flags);

			if (val & PCM_ENABLE_INPUT) {
				wave_dev->enablebits |= PCM_ENABLE_INPUT;
				if (wiinst->state & WAVE_STATE_OPEN)
					emu10k1_wavein_start(wave_dev);
			} else {
				wave_dev->enablebits &= ~PCM_ENABLE_INPUT;
				if (wiinst->state & WAVE_STATE_STARTED)
					emu10k1_wavein_stop(wave_dev);
			}

			spin_unlock_irqrestore(&wiinst->lock, flags);
		}
		break;

	case SNDCTL_DSP_GETOSPACE:
		{
			audio_buf_info info;

			DPF(4, "SNDCTL_DSP_GETOSPACE:\n");

			if (!(file->f_mode & FMODE_WRITE))
				return -EINVAL;

			spin_lock_irqsave(&woinst->lock, flags);

			if (woinst->state & WAVE_STATE_OPEN) {
				emu10k1_waveout_update(woinst);
				emu10k1_waveout_getxfersize(woinst, &bytestocopy);
				info.bytes = bytestocopy;
			} else {
				calculate_ofrag(woinst);
				info.bytes = woinst->buffer.size;
			}
			spin_unlock_irqrestore(&woinst->lock, flags);

			info.bytes *= woinst->num_voices;
			info.fragsize = woinst->buffer.fragment_size * woinst->num_voices;
			info.fragstotal = woinst->buffer.numfrags * woinst->num_voices;
			info.fragments = info.bytes / info.fragsize;

			if (copy_to_user(p, &info, sizeof(info)))
				return -EFAULT;
		}
		break;

	case SNDCTL_DSP_GETISPACE:
		{
			audio_buf_info info;

			DPF(4, "SNDCTL_DSP_GETISPACE:\n");

			if (!(file->f_mode & FMODE_READ))
				return -EINVAL;

			spin_lock_irqsave(&wiinst->lock, flags);
			if (wiinst->state & WAVE_STATE_OPEN) {
				emu10k1_wavein_update(wave_dev->card, wiinst);
				emu10k1_wavein_getxfersize(wiinst, &bytestocopy);
				info.bytes = bytestocopy;
			} else {
				calculate_ifrag(wiinst);
				info.bytes = 0;
			}
			spin_unlock_irqrestore(&wiinst->lock, flags);

			info.fragstotal = wiinst->buffer.numfrags;
			info.fragments = info.bytes / wiinst->buffer.fragment_size;
			info.fragsize = wiinst->buffer.fragment_size;

			if (copy_to_user(p, &info, sizeof(info)))
				return -EFAULT;
		}
		break;

	case SNDCTL_DSP_NONBLOCK:
		DPF(2, "SNDCTL_DSP_NONBLOCK:\n");

		file->f_flags |= O_NONBLOCK;
		break;

	case SNDCTL_DSP_GETODELAY:
		DPF(4, "SNDCTL_DSP_GETODELAY:\n");

		if (!(file->f_mode & FMODE_WRITE))
			return -EINVAL;

		spin_lock_irqsave(&woinst->lock, flags);
		if (woinst->state & WAVE_STATE_OPEN) {
			emu10k1_waveout_update(woinst);
			emu10k1_waveout_getxfersize(woinst, &bytestocopy);
			val = woinst->buffer.size - bytestocopy;
		} else
			val = 0;

		val *= woinst->num_voices;
		spin_unlock_irqrestore(&woinst->lock, flags);

		return put_user(val, p);

	case SNDCTL_DSP_GETIPTR:
		{
			count_info cinfo;

			DPF(4, "SNDCTL_DSP_GETIPTR: \n");

			if (!(file->f_mode & FMODE_READ))
				return -EINVAL;

			spin_lock_irqsave(&wiinst->lock, flags);

			if (wiinst->state & WAVE_STATE_OPEN) {
				emu10k1_wavein_update(wave_dev->card, wiinst);
				cinfo.ptr = wiinst->buffer.hw_pos;
				cinfo.bytes = cinfo.ptr + wiinst->total_recorded - wiinst->total_recorded % wiinst->buffer.size;
				cinfo.blocks = cinfo.bytes / wiinst->buffer.fragment_size - wiinst->blocks;
				wiinst->blocks = cinfo.bytes / wiinst->buffer.fragment_size;
			} else {
				cinfo.ptr = 0;
				cinfo.bytes = 0;
				cinfo.blocks = 0;
			}

			if (wiinst->mmapped)
				wiinst->buffer.bytestocopy %= wiinst->buffer.fragment_size;

			spin_unlock_irqrestore(&wiinst->lock, flags);

			if (copy_to_user(p, &cinfo, sizeof(cinfo)))
				return -EFAULT;
		}
		break;

	case SNDCTL_DSP_GETOPTR:
		{
			count_info cinfo;

			DPF(4, "SNDCTL_DSP_GETOPTR:\n");

			if (!(file->f_mode & FMODE_WRITE))
				return -EINVAL;

			spin_lock_irqsave(&woinst->lock, flags);

			if (woinst->state & WAVE_STATE_OPEN || 
			    ((woinst->format.passthrough == 1) && wave_dev->card->pt.state)) {
				int num_fragments;

				if (woinst->format.passthrough == 1) {
					emu10k1_pt_waveout_update(wave_dev);
					cinfo.bytes = woinst->total_played;
				} else {
					emu10k1_waveout_update(woinst);
					cinfo.bytes = woinst->total_played;
				}

				cinfo.ptr = woinst->buffer.hw_pos;
				num_fragments = cinfo.bytes / woinst->buffer.fragment_size;
				cinfo.blocks = num_fragments - woinst->blocks;
				woinst->blocks = num_fragments;

				cinfo.bytes *= woinst->num_voices;
				cinfo.ptr *= woinst->num_voices;
			} else {
				cinfo.ptr = 0;
				cinfo.bytes = 0;
				cinfo.blocks = 0;
			}

			if (woinst->mmapped)
				woinst->buffer.free_bytes %= woinst->buffer.fragment_size;

			spin_unlock_irqrestore(&woinst->lock, flags);

			if (copy_to_user(p, &cinfo, sizeof(cinfo)))
				return -EFAULT;
		}
		break;

	case SNDCTL_DSP_GETBLKSIZE:
		DPF(2, "SNDCTL_DSP_GETBLKSIZE:\n");

		if (file->f_mode & FMODE_WRITE) {
			spin_lock_irqsave(&woinst->lock, flags);

			calculate_ofrag(woinst);
			val = woinst->buffer.fragment_size * woinst->num_voices;

			spin_unlock_irqrestore(&woinst->lock, flags);
		}

		if (file->f_mode & FMODE_READ) {
			spin_lock_irqsave(&wiinst->lock, flags);

			calculate_ifrag(wiinst);
			val = wiinst->buffer.fragment_size;

			spin_unlock_irqrestore(&wiinst->lock, flags);
		}

		return put_user(val, p);

		break;

	case SNDCTL_DSP_POST:
		if (file->f_mode & FMODE_WRITE) {
			spin_lock_irqsave(&woinst->lock, flags);

			if (!(woinst->state & WAVE_STATE_STARTED)
			    && (wave_dev->enablebits & PCM_ENABLE_OUTPUT)
			    && (woinst->total_copied > 0))
				emu10k1_waveout_start(wave_dev);

			spin_unlock_irqrestore(&woinst->lock, flags);
		}

		break;

	case SNDCTL_DSP_SUBDIVIDE:
		DPF(2, "SNDCTL_DSP_SUBDIVIDE: not implemented\n");
		break;

	case SNDCTL_DSP_SETFRAGMENT:
		DPF(2, "SNDCTL_DSP_SETFRAGMENT:\n");

		if (get_user(val, p))
			return -EFAULT;

		DPD(2, "val is %#x\n", val);

		if (val == 0)
			return -EIO;

		if (file->f_mode & FMODE_WRITE) {
			/* digital pass-through fragment count and size are fixed values */
			if (woinst->state & WAVE_STATE_OPEN || (woinst->format.passthrough == 1))
				return -EINVAL;	/* too late to change */

			woinst->buffer.ossfragshift = val & 0xffff;
			woinst->buffer.numfrags = (val >> 16) & 0xffff;
		}

		if (file->f_mode & FMODE_READ) {
			if (wiinst->state & WAVE_STATE_OPEN)
				return -EINVAL;	/* too late to change */

			wiinst->buffer.ossfragshift = val & 0xffff;
			wiinst->buffer.numfrags = (val >> 16) & 0xffff;
		}

		break;

	case SNDCTL_COPR_LOAD:
		{
			copr_buffer *buf;
			u32 i;

			DPF(4, "SNDCTL_COPR_LOAD:\n");

			buf = kmalloc(sizeof(copr_buffer), GFP_KERNEL);
			if (!buf)
				return -ENOMEM;

			if (copy_from_user(buf, p, sizeof(copr_buffer))) {
				kfree (buf);
				return -EFAULT;
			}

			if ((buf->command != CMD_READ) && (buf->command != CMD_WRITE)) {
				kfree (buf);
				return -EINVAL;
			}

			if (buf->command == CMD_WRITE) {
				
#ifdef DBGEMU
				if ((buf->offs < 0) || (buf->offs + buf->len > 0xe00) || (buf->len > 1000)) {
#else
				if (((buf->offs < 0x100) || (buf->offs + buf->len > (wave_dev->card->is_audigy ? 0xe00 : 0x800)) || (buf->len > 1000)
				) && !(
					//any register allowed raw access to users goes here:
					(buf->offs == DBG ||
					  buf->offs == A_DBG)
					&& (buf->len == 1))) {
#endif		
					kfree(buf);
					return -EINVAL;
				}
			} else {
				if ((buf->offs < 0) || (buf->offs + buf->len > 0xe00) || (buf->len > 1000)) {
					kfree(buf);
					return -EINVAL;
				}
			}			
				
			if (((unsigned)buf->flags) > 0x3f)
				buf->flags = 0;

			if (buf->command == CMD_READ) {
				for (i = 0; i < buf->len; i++)
					((u32 *) buf->data)[i] = sblive_readptr(wave_dev->card, buf->offs + i, buf->flags);

				if (copy_to_user(p, buf, sizeof(copr_buffer))) {
					kfree(buf);
					return -EFAULT;
				}
			} else {
				for (i = 0; i < buf->len; i++)
					sblive_writeptr(wave_dev->card, buf->offs + i, buf->flags, ((u32 *) buf->data)[i]);
			}

			kfree (buf);
			break;
		}

	default:		/* Default is unrecognized command */
		DPD(2, "default: %#x\n", cmd);
		return -EINVAL;
	}
	return 0;
}

static struct page *emu10k1_mm_nopage (struct vm_area_struct * vma, unsigned long address, int *type)
{
	struct emu10k1_wavedevice *wave_dev = vma->vm_private_data;
	struct woinst *woinst = wave_dev->woinst;
	struct wiinst *wiinst = wave_dev->wiinst;
	struct page *dmapage;
	unsigned long pgoff;
	int rd, wr;

	DPF(3, "emu10k1_mm_nopage()\n");
	DPD(3, "addr: %#lx\n", address);

	if (address > vma->vm_end) {
		DPF(1, "EXIT, returning NOPAGE_SIGBUS\n");
		return NOPAGE_SIGBUS; /* Disallow mremap */
	}

	pgoff = vma->vm_pgoff + ((address - vma->vm_start) >> PAGE_SHIFT);
	if (woinst != NULL)
		wr = woinst->mmapped;
	else
		wr = 0;

	if (wiinst != NULL)
		rd = wiinst->mmapped;
	else
		rd = 0;

	/* if full-duplex (read+write) and we have two sets of bufs,
	* then the playback buffers come first, sez soundcard.c */
	if (wr) {
		if (pgoff >= woinst->buffer.pages) {
			pgoff -= woinst->buffer.pages;
			dmapage = virt_to_page ((u8 *) wiinst->buffer.addr + pgoff * PAGE_SIZE);
		} else
			dmapage = virt_to_page (woinst->voice[0].mem.addr[pgoff]);
	} else {
		dmapage = virt_to_page ((u8 *) wiinst->buffer.addr + pgoff * PAGE_SIZE);
	}

	get_page (dmapage);

	DPD(3, "page: %#lx\n", (unsigned long) dmapage);
	if (type)
		*type = VM_FAULT_MINOR;
	return dmapage;
}

static struct vm_operations_struct emu10k1_mm_ops = {
	.nopage         = emu10k1_mm_nopage,
};

static int emu10k1_audio_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct emu10k1_wavedevice *wave_dev = (struct emu10k1_wavedevice *) file->private_data;
	unsigned long max_pages, n_pages, pgoffset;
	struct woinst *woinst = NULL;
	struct wiinst *wiinst = NULL;
	unsigned long flags;

	DPF(2, "emu10k1_audio_mmap()\n");

	max_pages = 0;
	if (vma->vm_flags & VM_WRITE) {
		woinst = wave_dev->woinst;

		spin_lock_irqsave(&woinst->lock, flags);

		/* No m'mapping possible for multichannel */
		if (woinst->num_voices > 1) {
			spin_unlock_irqrestore(&woinst->lock, flags);
                	return -EINVAL;
		}

		if (woinst->state == WAVE_STATE_CLOSED) {
			calculate_ofrag(woinst);

			if (emu10k1_waveout_open(wave_dev) < 0) {
				spin_unlock_irqrestore(&woinst->lock, flags);
				ERROR();
				return -EINVAL;
			}
		}

		woinst->mmapped = 1;
		max_pages += woinst->buffer.pages;
		spin_unlock_irqrestore(&woinst->lock, flags);
	}

	if (vma->vm_flags & VM_READ) {
		wiinst = wave_dev->wiinst;

		spin_lock_irqsave(&wiinst->lock, flags);
		if (wiinst->state == WAVE_STATE_CLOSED) {
			calculate_ifrag(wiinst);

			if (emu10k1_wavein_open(wave_dev) < 0) {
				spin_unlock_irqrestore(&wiinst->lock, flags);
				ERROR();
				return -EINVAL;
			}
		}

		wiinst->mmapped = 1;
		max_pages += wiinst->buffer.pages;
		spin_unlock_irqrestore(&wiinst->lock, flags);
	}

	n_pages = ((vma->vm_end - vma->vm_start) + PAGE_SIZE - 1) >> PAGE_SHIFT;
	pgoffset = vma->vm_pgoff;

	DPD(2, "vma_start: %#lx, vma_end: %#lx, vma_offset: %ld\n", vma->vm_start, vma->vm_end, pgoffset);
	DPD(2, "n_pages: %ld, max_pages: %ld\n", n_pages, max_pages);

	if (pgoffset + n_pages > max_pages)
		return -EINVAL;

	vma->vm_flags |= VM_RESERVED;
	vma->vm_ops = &emu10k1_mm_ops;
	vma->vm_private_data = wave_dev;
	return 0;
}

static int emu10k1_audio_open(struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	struct emu10k1_card *card = NULL;
	struct list_head *entry;
	struct emu10k1_wavedevice *wave_dev;

	DPF(2, "emu10k1_audio_open()\n");

	/* Check for correct device to open */

	list_for_each(entry, &emu10k1_devs) {
		card = list_entry(entry, struct emu10k1_card, list);

		if (!((card->audio_dev ^ minor) & ~0xf) || !((card->audio_dev1 ^ minor) & ~0xf))
			goto match;
	}

	return -ENODEV;

match:

	wave_dev = kmalloc(sizeof(struct emu10k1_wavedevice), GFP_KERNEL);

	if (wave_dev == NULL) { 
		ERROR();
		return -ENOMEM;
	}

	wave_dev->card = card;
	wave_dev->wiinst = NULL;
	wave_dev->woinst = NULL;
	wave_dev->enablebits = PCM_ENABLE_OUTPUT | PCM_ENABLE_INPUT;	/* Default */

	if (file->f_mode & FMODE_READ) {
		/* Recording */
		struct wiinst *wiinst;

		if ((wiinst = kmalloc(sizeof(struct wiinst), GFP_KERNEL)) == NULL) {
			ERROR();
			kfree(wave_dev);
			return -ENOMEM;
		}

		wiinst->recsrc = card->wavein.recsrc;
                wiinst->fxwc = card->wavein.fxwc;

		switch (wiinst->recsrc) {
		case WAVERECORD_AC97:
			wiinst->format.id = AFMT_S16_LE;
			wiinst->format.samplingrate = 8000;
			wiinst->format.bitsperchannel = 16;
			wiinst->format.channels = 1;
			break;
		case WAVERECORD_MIC:
			wiinst->format.id = AFMT_S16_LE;
			wiinst->format.samplingrate = 8000;
			wiinst->format.bitsperchannel = 16;
			wiinst->format.channels = 1;
			break;
		case WAVERECORD_FX:
			wiinst->format.id = AFMT_S16_LE;
			wiinst->format.samplingrate = 48000;
			wiinst->format.bitsperchannel = 16;
			wiinst->format.channels = hweight32(wiinst->fxwc);
			break;
		default:
			kfree(wave_dev);
			kfree(wiinst);
			BUG();
			break;
		}

		wiinst->state = WAVE_STATE_CLOSED;

		wiinst->buffer.ossfragshift = 0;
		wiinst->buffer.fragment_size = 0;
		wiinst->buffer.numfrags = 0;

		init_waitqueue_head(&wiinst->wait_queue);

		wiinst->mmapped = 0;
		wiinst->total_recorded = 0;
		wiinst->blocks = 0;
		spin_lock_init(&wiinst->lock);
		tasklet_init(&wiinst->timer.tasklet, emu10k1_wavein_bh, (unsigned long) wave_dev);
		wave_dev->wiinst = wiinst;
		emu10k1_wavein_setformat(wave_dev, &wiinst->format);
	}

	if (file->f_mode & FMODE_WRITE) {
		struct woinst *woinst;
		int i;

		if ((woinst = kmalloc(sizeof(struct woinst), GFP_KERNEL)) == NULL) {
			ERROR();
			kfree(wave_dev);
			return -ENOMEM;
		}

		if (wave_dev->wiinst != NULL) {
			woinst->format = wave_dev->wiinst->format;
		} else {
			woinst->format.id = AFMT_U8;
			woinst->format.samplingrate = 8000;
			woinst->format.bitsperchannel = 8;
			woinst->format.channels = 1;
		}

		woinst->state = WAVE_STATE_CLOSED;

		woinst->buffer.fragment_size = 0;
		woinst->buffer.ossfragshift = 0;
		woinst->buffer.numfrags = 0;
		woinst->device = (card->audio_dev1 == minor);
		woinst->timer.state = TIMER_STATE_UNINSTALLED;
		woinst->num_voices = 1;
		for (i = 0; i < WAVEOUT_MAXVOICES; i++) {
			woinst->voice[i].usage = VOICE_USAGE_FREE;
			woinst->voice[i].mem.emupageindex = -1;
		}

		init_waitqueue_head(&woinst->wait_queue);

		woinst->mmapped = 0;
		woinst->total_copied = 0;
		woinst->total_played = 0;
		woinst->blocks = 0;
		spin_lock_init(&woinst->lock);
		tasklet_init(&woinst->timer.tasklet, emu10k1_waveout_bh, (unsigned long) wave_dev);
		wave_dev->woinst = woinst;
		emu10k1_waveout_setformat(wave_dev, &woinst->format);
	}

	file->private_data = (void *) wave_dev;

	return nonseekable_open(inode, file);
}

static int emu10k1_audio_release(struct inode *inode, struct file *file)
{
	struct emu10k1_wavedevice *wave_dev = (struct emu10k1_wavedevice *) file->private_data;
	struct emu10k1_card *card;
	unsigned long flags;

	card = wave_dev->card;

	DPF(2, "emu10k1_audio_release()\n");

	if (file->f_mode & FMODE_WRITE) {
		struct woinst *woinst = wave_dev->woinst;

		spin_lock_irqsave(&woinst->lock, flags);
		if(woinst->format.passthrough==2)
			card->pt.state=PT_STATE_PLAYING;
		if (woinst->format.passthrough && card->pt.state != PT_STATE_INACTIVE){
			spin_lock(&card->pt.lock);
                        emu10k1_pt_stop(card);
			spin_unlock(&card->pt.lock);
		}
		if (woinst->state & WAVE_STATE_OPEN) {
			if (woinst->state & WAVE_STATE_STARTED) {
				if (!(file->f_flags & O_NONBLOCK)) {
					while (!signal_pending(current)
					       && (woinst->total_played < woinst->total_copied)) {
						DPF(4, "Buffer hasn't been totally played, sleep....\n");
						spin_unlock_irqrestore(&woinst->lock, flags);
						interruptible_sleep_on(&woinst->wait_queue);
						spin_lock_irqsave(&woinst->lock, flags);
					}
				}
			}
			emu10k1_waveout_close(wave_dev);
		}

		spin_unlock_irqrestore(&woinst->lock, flags);
		/* remove the tasklet */
		tasklet_kill(&woinst->timer.tasklet);
		kfree(wave_dev->woinst);
	}

	if (file->f_mode & FMODE_READ) {
		struct wiinst *wiinst = wave_dev->wiinst;

		spin_lock_irqsave(&wiinst->lock, flags);

		if (wiinst->state & WAVE_STATE_OPEN) {
			emu10k1_wavein_close(wave_dev);
		}

		spin_unlock_irqrestore(&wiinst->lock, flags);
		tasklet_kill(&wiinst->timer.tasklet);
		kfree(wave_dev->wiinst);
	}

	kfree(wave_dev);

	if (waitqueue_active(&card->open_wait))
		wake_up_interruptible(&card->open_wait);

	return 0;
}

/* FIXME sort out poll() + mmap() */
static unsigned int emu10k1_audio_poll(struct file *file, struct poll_table_struct *wait)
{
	struct emu10k1_wavedevice *wave_dev = (struct emu10k1_wavedevice *) file->private_data;
	struct woinst *woinst = wave_dev->woinst;
	struct wiinst *wiinst = wave_dev->wiinst;
	unsigned int mask = 0;
	u32 bytestocopy;
	unsigned long flags;

	DPF(4, "emu10k1_audio_poll()\n");

	if (file->f_mode & FMODE_WRITE)
		poll_wait(file, &woinst->wait_queue, wait);

	if (file->f_mode & FMODE_READ)
		poll_wait(file, &wiinst->wait_queue, wait);

	if (file->f_mode & FMODE_WRITE) {
		spin_lock_irqsave(&woinst->lock, flags);

		if (woinst->state & WAVE_STATE_OPEN) {
			emu10k1_waveout_update(woinst);
			emu10k1_waveout_getxfersize(woinst, &bytestocopy);

			if (bytestocopy >= woinst->buffer.fragment_size)
				mask |= POLLOUT | POLLWRNORM;
		} else
			mask |= POLLOUT | POLLWRNORM;

		spin_unlock_irqrestore(&woinst->lock, flags);
	}

	if (file->f_mode & FMODE_READ) {
		spin_lock_irqsave(&wiinst->lock, flags);

		if (wiinst->state & WAVE_STATE_OPEN) {
			emu10k1_wavein_update(wave_dev->card, wiinst);
			emu10k1_wavein_getxfersize(wiinst, &bytestocopy);

			if (bytestocopy >= wiinst->buffer.fragment_size)
				mask |= POLLIN | POLLRDNORM;
		}

		spin_unlock_irqrestore(&wiinst->lock, flags);
	}

	return mask;
}

static void calculate_ofrag(struct woinst *woinst)
{
	struct waveout_buffer *buffer = &woinst->buffer;
	u32 fragsize;

	if (buffer->fragment_size)
		return;

	if (!buffer->ossfragshift) {
		fragsize = (woinst->format.bytespervoicesample * woinst->format.samplingrate * WAVEOUT_DEFAULTFRAGLEN) / 1000 - 1;

		while (fragsize) {
			fragsize >>= 1;
			buffer->ossfragshift++;
		}
	}

	if (buffer->ossfragshift < WAVEOUT_MINFRAGSHIFT)
		buffer->ossfragshift = WAVEOUT_MINFRAGSHIFT;

	buffer->fragment_size = 1 << buffer->ossfragshift;

	while (buffer->fragment_size * WAVEOUT_MINFRAGS > WAVEOUT_MAXBUFSIZE)
		buffer->fragment_size >>= 1;

	/* now we are sure that:
	 (2^WAVEOUT_MINFRAGSHIFT) <= (fragment_size = 2^n) <= (WAVEOUT_MAXBUFSIZE / WAVEOUT_MINFRAGS)
	*/

	if (!buffer->numfrags) {
		u32 numfrags;

		numfrags = (woinst->format.bytespervoicesample * woinst->format.samplingrate * WAVEOUT_DEFAULTBUFLEN) /
			   (buffer->fragment_size * 1000) - 1;

		buffer->numfrags = 1;

		while (numfrags) {
			numfrags >>= 1;
			buffer->numfrags <<= 1;
		}
	}

	if (buffer->numfrags < WAVEOUT_MINFRAGS)
		buffer->numfrags = WAVEOUT_MINFRAGS;

	if (buffer->numfrags * buffer->fragment_size > WAVEOUT_MAXBUFSIZE)
		buffer->numfrags = WAVEOUT_MAXBUFSIZE / buffer->fragment_size;

	if (buffer->numfrags < WAVEOUT_MINFRAGS)
		BUG();

	buffer->size = buffer->fragment_size * buffer->numfrags;
	buffer->pages = buffer->size / PAGE_SIZE + ((buffer->size % PAGE_SIZE) ? 1 : 0);

	DPD(2, " calculated playback fragment_size -> %d\n", buffer->fragment_size);
	DPD(2, " calculated playback numfrags -> %d\n", buffer->numfrags);

	return;
}

static void calculate_ifrag(struct wiinst *wiinst)
{
	struct wavein_buffer *buffer = &wiinst->buffer;
	u32 fragsize, bufsize, size[4];
	int i, j;

	if (buffer->fragment_size)
		return;

	if (!buffer->ossfragshift) {
		fragsize = (wiinst->format.bytespersec * WAVEIN_DEFAULTFRAGLEN) / 1000 - 1;

		while (fragsize) {
			fragsize >>= 1;
			buffer->ossfragshift++;
		}
	}

	if (buffer->ossfragshift < WAVEIN_MINFRAGSHIFT)
		buffer->ossfragshift = WAVEIN_MINFRAGSHIFT;

	buffer->fragment_size = 1 << buffer->ossfragshift;

	while (buffer->fragment_size * WAVEIN_MINFRAGS > WAVEIN_MAXBUFSIZE)
		buffer->fragment_size >>= 1;

	/* now we are sure that:
	   (2^WAVEIN_MINFRAGSHIFT) <= (fragment_size = 2^n) <= (WAVEIN_MAXBUFSIZE / WAVEIN_MINFRAGS)
        */


	if (!buffer->numfrags)
		buffer->numfrags = (wiinst->format.bytespersec * WAVEIN_DEFAULTBUFLEN) / (buffer->fragment_size * 1000) - 1;

	if (buffer->numfrags < WAVEIN_MINFRAGS)
		buffer->numfrags = WAVEIN_MINFRAGS;

	if (buffer->numfrags * buffer->fragment_size > WAVEIN_MAXBUFSIZE)
		buffer->numfrags = WAVEIN_MAXBUFSIZE / buffer->fragment_size;

	if (buffer->numfrags < WAVEIN_MINFRAGS)
		BUG();

	bufsize = buffer->fragment_size * buffer->numfrags;

	/* the buffer size for recording is restricted to certain values, adjust it now */
	if (bufsize >= 0x10000) {
		buffer->size = 0x10000;
		buffer->sizeregval = 0x1f;
	} else {
		buffer->size = 0;
		size[0] = 384;
		size[1] = 448;
		size[2] = 512;
		size[3] = 640;

		for (i = 0; i < 8; i++)
			for (j = 0; j < 4; j++)
				if (bufsize >= size[j]) {
					buffer->size = size[j];
					size[j] *= 2;
					buffer->sizeregval = i * 4 + j + 1;
				} else
					goto exitloop;
	      exitloop:
		if (buffer->size == 0) {
			buffer->size = 384;
			buffer->sizeregval = 0x01;
		}
	}

	/* adjust the fragment size so that buffer size is an integer multiple */
	while (buffer->size % buffer->fragment_size)
		buffer->fragment_size >>= 1;

	buffer->numfrags = buffer->size / buffer->fragment_size;
	buffer->pages =  buffer->size / PAGE_SIZE + ((buffer->size % PAGE_SIZE) ? 1 : 0);

	DPD(2, " calculated recording fragment_size -> %d\n", buffer->fragment_size);
	DPD(2, " calculated recording numfrags -> %d\n", buffer->numfrags);
	DPD(2, " buffer size register -> %#04x\n", buffer->sizeregval);

	return;
}

static void emu10k1_wavein_bh(unsigned long refdata)
{
	struct emu10k1_wavedevice *wave_dev = (struct emu10k1_wavedevice *) refdata;
	struct wiinst *wiinst = wave_dev->wiinst;
	u32 bytestocopy;
	unsigned long flags;

	if (!wiinst)
		return;

	spin_lock_irqsave(&wiinst->lock, flags);

	if (!(wiinst->state & WAVE_STATE_STARTED)) {
		spin_unlock_irqrestore(&wiinst->lock, flags);
		return;
	}

	emu10k1_wavein_update(wave_dev->card, wiinst);
	emu10k1_wavein_getxfersize(wiinst, &bytestocopy);

	spin_unlock_irqrestore(&wiinst->lock, flags);

	if (bytestocopy >= wiinst->buffer.fragment_size) {
	 	if (waitqueue_active(&wiinst->wait_queue))
			wake_up_interruptible(&wiinst->wait_queue);
	} else
		DPD(3, "Not enough transfer size, %d\n", bytestocopy);

	return;
}

static void emu10k1_waveout_bh(unsigned long refdata)
{
	struct emu10k1_wavedevice *wave_dev = (struct emu10k1_wavedevice *) refdata;
	struct woinst *woinst = wave_dev->woinst;
	u32 bytestocopy;
	unsigned long flags;

	if (!woinst)
		return;

	spin_lock_irqsave(&woinst->lock, flags);

	if (!(woinst->state & WAVE_STATE_STARTED)) {
		spin_unlock_irqrestore(&woinst->lock, flags);
		return;
	}

	emu10k1_waveout_update(woinst);
	emu10k1_waveout_getxfersize(woinst, &bytestocopy);

	if (woinst->buffer.fill_silence) {
		spin_unlock_irqrestore(&woinst->lock, flags);
		emu10k1_waveout_fillsilence(woinst);
	} else
		spin_unlock_irqrestore(&woinst->lock, flags);

	if (bytestocopy >= woinst->buffer.fragment_size) {
		if (waitqueue_active(&woinst->wait_queue))
			wake_up_interruptible(&woinst->wait_queue);
	} else
		DPD(3, "Not enough transfer size -> %d\n", bytestocopy);

	return;
}

struct file_operations emu10k1_audio_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.read		= emu10k1_audio_read,
	.write		= emu10k1_audio_write,
	.poll		= emu10k1_audio_poll,
	.ioctl		= emu10k1_audio_ioctl,
	.mmap		= emu10k1_audio_mmap,
	.open		= emu10k1_audio_open,
	.release	= emu10k1_audio_release,
};
