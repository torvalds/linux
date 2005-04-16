/*
 * OSS compatible sequencer driver
 *
 * OSS compatible i/o control
 *
 * Copyright (C) 1998,99 Takashi Iwai <tiwai@suse.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include "seq_oss_device.h"
#include "seq_oss_readq.h"
#include "seq_oss_writeq.h"
#include "seq_oss_timer.h"
#include "seq_oss_synth.h"
#include "seq_oss_midi.h"
#include "seq_oss_event.h"

static int snd_seq_oss_synth_info_user(seq_oss_devinfo_t *dp, void __user *arg)
{
	struct synth_info info;

	if (copy_from_user(&info, arg, sizeof(info)))
		return -EFAULT;
	if (snd_seq_oss_synth_make_info(dp, info.device, &info) < 0)
		return -EINVAL;
	if (copy_to_user(arg, &info, sizeof(info)))
		return -EFAULT;
	return 0;
}

static int snd_seq_oss_midi_info_user(seq_oss_devinfo_t *dp, void __user *arg)
{
	struct midi_info info;

	if (copy_from_user(&info, arg, sizeof(info)))
		return -EFAULT;
	if (snd_seq_oss_midi_make_info(dp, info.device, &info) < 0)
		return -EINVAL;
	if (copy_to_user(arg, &info, sizeof(info)))
		return -EFAULT;
	return 0;
}

static int snd_seq_oss_oob_user(seq_oss_devinfo_t *dp, void __user *arg)
{
	unsigned char ev[8];
	snd_seq_event_t tmpev;

	if (copy_from_user(ev, arg, 8))
		return -EFAULT;
	memset(&tmpev, 0, sizeof(tmpev));
	snd_seq_oss_fill_addr(dp, &tmpev, dp->addr.port, dp->addr.client);
	tmpev.time.tick = 0;
	if (! snd_seq_oss_process_event(dp, (evrec_t*)ev, &tmpev)) {
		snd_seq_oss_dispatch(dp, &tmpev, 0, 0);
	}
	return 0;
}

int
snd_seq_oss_ioctl(seq_oss_devinfo_t *dp, unsigned int cmd, unsigned long carg)
{
	int dev, val;
	void __user *arg = (void __user *)carg;
	int __user *p = arg;

	switch (cmd) {
	case SNDCTL_TMR_TIMEBASE:
	case SNDCTL_TMR_TEMPO:
	case SNDCTL_TMR_START:
	case SNDCTL_TMR_STOP:
	case SNDCTL_TMR_CONTINUE:
	case SNDCTL_TMR_METRONOME:
	case SNDCTL_TMR_SOURCE:
	case SNDCTL_TMR_SELECT:
	case SNDCTL_SEQ_CTRLRATE:
		return snd_seq_oss_timer_ioctl(dp->timer, cmd, arg);

	case SNDCTL_SEQ_PANIC:
		debug_printk(("panic\n"));
		snd_seq_oss_reset(dp);
		return -EINVAL;

	case SNDCTL_SEQ_SYNC:
		debug_printk(("sync\n"));
		if (! is_write_mode(dp->file_mode) || dp->writeq == NULL)
			return 0;
		while (snd_seq_oss_writeq_sync(dp->writeq))
			;
		if (signal_pending(current))
			return -ERESTARTSYS;
		return 0;

	case SNDCTL_SEQ_RESET:
		debug_printk(("reset\n"));
		snd_seq_oss_reset(dp);
		return 0;

	case SNDCTL_SEQ_TESTMIDI:
		debug_printk(("test midi\n"));
		if (get_user(dev, p))
			return -EFAULT;
		return snd_seq_oss_midi_open(dp, dev, dp->file_mode);

	case SNDCTL_SEQ_GETINCOUNT:
		debug_printk(("get in count\n"));
		if (dp->readq == NULL || ! is_read_mode(dp->file_mode))
			return 0;
		return put_user(dp->readq->qlen, p) ? -EFAULT : 0;

	case SNDCTL_SEQ_GETOUTCOUNT:
		debug_printk(("get out count\n"));
		if (! is_write_mode(dp->file_mode) || dp->writeq == NULL)
			return 0;
		return put_user(snd_seq_oss_writeq_get_free_size(dp->writeq), p) ? -EFAULT : 0;

	case SNDCTL_SEQ_GETTIME:
		debug_printk(("get time\n"));
		return put_user(snd_seq_oss_timer_cur_tick(dp->timer), p) ? -EFAULT : 0;

	case SNDCTL_SEQ_RESETSAMPLES:
		debug_printk(("reset samples\n"));
		if (get_user(dev, p))
			return -EFAULT;
		return snd_seq_oss_synth_ioctl(dp, dev, cmd, carg);

	case SNDCTL_SEQ_NRSYNTHS:
		debug_printk(("nr synths\n"));
		return put_user(dp->max_synthdev, p) ? -EFAULT : 0;

	case SNDCTL_SEQ_NRMIDIS:
		debug_printk(("nr midis\n"));
		return put_user(dp->max_mididev, p) ? -EFAULT : 0;

	case SNDCTL_SYNTH_MEMAVL:
		debug_printk(("mem avail\n"));
		if (get_user(dev, p))
			return -EFAULT;
		val = snd_seq_oss_synth_ioctl(dp, dev, cmd, carg);
		return put_user(val, p) ? -EFAULT : 0;

	case SNDCTL_FM_4OP_ENABLE:
		debug_printk(("4op\n"));
		if (get_user(dev, p))
			return -EFAULT;
		snd_seq_oss_synth_ioctl(dp, dev, cmd, carg);
		return 0;

	case SNDCTL_SYNTH_INFO:
	case SNDCTL_SYNTH_ID:
		debug_printk(("synth info\n"));
		return snd_seq_oss_synth_info_user(dp, arg);

	case SNDCTL_SEQ_OUTOFBAND:
		debug_printk(("out of band\n"));
		return snd_seq_oss_oob_user(dp, arg);

	case SNDCTL_MIDI_INFO:
		debug_printk(("midi info\n"));
		return snd_seq_oss_midi_info_user(dp, arg);

	case SNDCTL_SEQ_THRESHOLD:
		debug_printk(("threshold\n"));
		if (! is_write_mode(dp->file_mode))
			return 0;
		if (get_user(val, p))
			return -EFAULT;
		if (val < 1)
			val = 1;
		if (val >= dp->writeq->maxlen)
			val = dp->writeq->maxlen - 1;
		snd_seq_oss_writeq_set_output(dp->writeq, val);
		return 0;

	case SNDCTL_MIDI_PRETIME:
		debug_printk(("pretime\n"));
		if (dp->readq == NULL || !is_read_mode(dp->file_mode))
			return 0;
		if (get_user(val, p))
			return -EFAULT;
		if (val <= 0)
			val = -1;
		else
			val = (HZ * val) / 10;
		dp->readq->pre_event_timeout = val;
		return put_user(val, p) ? -EFAULT : 0;

	default:
		debug_printk(("others\n"));
		if (! is_write_mode(dp->file_mode))
			return -EIO;
		return snd_seq_oss_synth_ioctl(dp, 0, cmd, carg);
	}
	return 0;
}

