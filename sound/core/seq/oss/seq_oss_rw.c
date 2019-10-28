/*
 * OSS compatible sequencer driver
 *
 * read/write/select interface to device file
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
#include "seq_oss_synth.h"
#include <sound/seq_oss_legacy.h>
#include "seq_oss_event.h"
#include "seq_oss_timer.h"
#include "../seq_clientmgr.h"


/*
 * protoypes
 */
static int insert_queue(struct seq_oss_devinfo *dp, union evrec *rec, struct file *opt);


/*
 * read interface
 */

int
snd_seq_oss_read(struct seq_oss_devinfo *dp, char __user *buf, int count)
{
	struct seq_oss_readq *readq = dp->readq;
	int result = 0, err = 0;
	int ev_len;
	union evrec rec;
	unsigned long flags;

	if (readq == NULL || ! is_read_mode(dp->file_mode))
		return -ENXIO;

	while (count >= SHORT_EVENT_SIZE) {
		snd_seq_oss_readq_lock(readq, flags);
		err = snd_seq_oss_readq_pick(readq, &rec);
		if (err == -EAGAIN &&
		    !is_nonblock_mode(dp->file_mode) && result == 0) {
			snd_seq_oss_readq_unlock(readq, flags);
			snd_seq_oss_readq_wait(readq);
			snd_seq_oss_readq_lock(readq, flags);
			if (signal_pending(current))
				err = -ERESTARTSYS;
			else
				err = snd_seq_oss_readq_pick(readq, &rec);
		}
		if (err < 0) {
			snd_seq_oss_readq_unlock(readq, flags);
			break;
		}
		ev_len = ev_length(&rec);
		if (ev_len < count) {
			snd_seq_oss_readq_unlock(readq, flags);
			break;
		}
		snd_seq_oss_readq_free(readq);
		snd_seq_oss_readq_unlock(readq, flags);
		if (copy_to_user(buf, &rec, ev_len)) {
			err = -EFAULT;
			break;
		}
		result += ev_len;
		buf += ev_len;
		count -= ev_len;
	}
	return result > 0 ? result : err;
}


/*
 * write interface
 */

int
snd_seq_oss_write(struct seq_oss_devinfo *dp, const char __user *buf, int count, struct file *opt)
{
	int result = 0, err = 0;
	int ev_size, fmt;
	union evrec rec;

	if (! is_write_mode(dp->file_mode) || dp->writeq == NULL)
		return -ENXIO;

	while (count >= SHORT_EVENT_SIZE) {
		if (copy_from_user(&rec, buf, SHORT_EVENT_SIZE)) {
			err = -EFAULT;
			break;
		}
		if (rec.s.code == SEQ_FULLSIZE) {
			/* load patch */
			if (result > 0) {
				err = -EINVAL;
				break;
			}
			fmt = (*(unsigned short *)rec.c) & 0xffff;
			/* FIXME the return value isn't correct */
			return snd_seq_oss_synth_load_patch(dp, rec.s.dev,
							    fmt, buf, 0, count);
		}
		if (ev_is_long(&rec)) {
			/* extended code */
			if (rec.s.code == SEQ_EXTENDED &&
			    dp->seq_mode == SNDRV_SEQ_OSS_MODE_MUSIC) {
				err = -EINVAL;
				break;
			}
			ev_size = LONG_EVENT_SIZE;
			if (count < ev_size)
				break;
			/* copy the reset 4 bytes */
			if (copy_from_user(rec.c + SHORT_EVENT_SIZE,
					   buf + SHORT_EVENT_SIZE,
					   LONG_EVENT_SIZE - SHORT_EVENT_SIZE)) {
				err = -EFAULT;
				break;
			}
		} else {
			/* old-type code */
			if (dp->seq_mode == SNDRV_SEQ_OSS_MODE_MUSIC) {
				err = -EINVAL;
				break;
			}
			ev_size = SHORT_EVENT_SIZE;
		}

		/* insert queue */
		if ((err = insert_queue(dp, &rec, opt)) < 0)
			break;

		result += ev_size;
		buf += ev_size;
		count -= ev_size;
	}
	return result > 0 ? result : err;
}


/*
 * insert event record to write queue
 * return: 0 = OK, non-zero = NG
 */
static int
insert_queue(struct seq_oss_devinfo *dp, union evrec *rec, struct file *opt)
{
	int rc = 0;
	struct snd_seq_event event;

	/* if this is a timing event, process the current time */
	if (snd_seq_oss_process_timer_event(dp->timer, rec))
		return 0; /* no need to insert queue */

	/* parse this event */
	memset(&event, 0, sizeof(event));
	/* set dummy -- to be sure */
	event.type = SNDRV_SEQ_EVENT_NOTEOFF;
	snd_seq_oss_fill_addr(dp, &event, dp->addr.client, dp->addr.port);

	if (snd_seq_oss_process_event(dp, rec, &event))
		return 0; /* invalid event - no need to insert queue */

	event.time.tick = snd_seq_oss_timer_cur_tick(dp->timer);
	if (dp->timer->realtime || !dp->timer->running) {
		snd_seq_oss_dispatch(dp, &event, 0, 0);
	} else {
		if (is_nonblock_mode(dp->file_mode))
			rc = snd_seq_kernel_client_enqueue(dp->cseq, &event, 0, 0);
		else
			rc = snd_seq_kernel_client_enqueue_blocking(dp->cseq, &event, opt, 0, 0);
	}
	return rc;
}
		

/*
 * select / poll
 */
  
__poll_t
snd_seq_oss_poll(struct seq_oss_devinfo *dp, struct file *file, poll_table * wait)
{
	__poll_t mask = 0;

	/* input */
	if (dp->readq && is_read_mode(dp->file_mode)) {
		if (snd_seq_oss_readq_poll(dp->readq, file, wait))
			mask |= EPOLLIN | EPOLLRDNORM;
	}

	/* output */
	if (dp->writeq && is_write_mode(dp->file_mode)) {
		if (snd_seq_kernel_client_write_poll(dp->cseq, file, wait))
			mask |= EPOLLOUT | EPOLLWRNORM;
	}
	return mask;
}
