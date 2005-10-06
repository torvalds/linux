/*
 * OSS compatible sequencer driver
 *
 * seq_oss_readq.c - MIDI input queue
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

#include "seq_oss_readq.h"
#include "seq_oss_event.h"
#include <sound/seq_oss_legacy.h>
#include "../seq_lock.h"
#include <linux/wait.h>

/*
 * constants
 */
//#define SNDRV_SEQ_OSS_MAX_TIMEOUT	(unsigned long)(-1)
#define SNDRV_SEQ_OSS_MAX_TIMEOUT	(HZ * 3600)


/*
 * prototypes
 */


/*
 * create a read queue
 */
seq_oss_readq_t *
snd_seq_oss_readq_new(seq_oss_devinfo_t *dp, int maxlen)
{
	seq_oss_readq_t *q;

	if ((q = kzalloc(sizeof(*q), GFP_KERNEL)) == NULL) {
		snd_printk(KERN_ERR "can't malloc read queue\n");
		return NULL;
	}

	if ((q->q = kcalloc(maxlen, sizeof(evrec_t), GFP_KERNEL)) == NULL) {
		snd_printk(KERN_ERR "can't malloc read queue buffer\n");
		kfree(q);
		return NULL;
	}

	q->maxlen = maxlen;
	q->qlen = 0;
	q->head = q->tail = 0;
	init_waitqueue_head(&q->midi_sleep);
	spin_lock_init(&q->lock);
	q->pre_event_timeout = SNDRV_SEQ_OSS_MAX_TIMEOUT;
	q->input_time = (unsigned long)-1;

	return q;
}

/*
 * delete the read queue
 */
void
snd_seq_oss_readq_delete(seq_oss_readq_t *q)
{
	if (q) {
		kfree(q->q);
		kfree(q);
	}
}

/*
 * reset the read queue
 */
void
snd_seq_oss_readq_clear(seq_oss_readq_t *q)
{
	if (q->qlen) {
		q->qlen = 0;
		q->head = q->tail = 0;
	}
	/* if someone sleeping, wake'em up */
	if (waitqueue_active(&q->midi_sleep))
		wake_up(&q->midi_sleep);
	q->input_time = (unsigned long)-1;
}

/*
 * put a midi byte
 */
int
snd_seq_oss_readq_puts(seq_oss_readq_t *q, int dev, unsigned char *data, int len)
{
	evrec_t rec;
	int result;

	memset(&rec, 0, sizeof(rec));
	rec.c[0] = SEQ_MIDIPUTC;
	rec.c[2] = dev;

	while (len-- > 0) {
		rec.c[1] = *data++;
		result = snd_seq_oss_readq_put_event(q, &rec);
		if (result < 0)
			return result;
	}
	return 0;
}

/*
 * copy an event to input queue:
 * return zero if enqueued
 */
int
snd_seq_oss_readq_put_event(seq_oss_readq_t *q, evrec_t *ev)
{
	unsigned long flags;

	spin_lock_irqsave(&q->lock, flags);
	if (q->qlen >= q->maxlen - 1) {
		spin_unlock_irqrestore(&q->lock, flags);
		return -ENOMEM;
	}

	memcpy(&q->q[q->tail], ev, sizeof(*ev));
	q->tail = (q->tail + 1) % q->maxlen;
	q->qlen++;

	/* wake up sleeper */
	if (waitqueue_active(&q->midi_sleep))
		wake_up(&q->midi_sleep);

	spin_unlock_irqrestore(&q->lock, flags);

	return 0;
}


/*
 * pop queue
 * caller must hold lock
 */
int
snd_seq_oss_readq_pick(seq_oss_readq_t *q, evrec_t *rec)
{
	if (q->qlen == 0)
		return -EAGAIN;
	memcpy(rec, &q->q[q->head], sizeof(*rec));
	return 0;
}

/*
 * sleep until ready
 */
void
snd_seq_oss_readq_wait(seq_oss_readq_t *q)
{
	wait_event_interruptible_timeout(q->midi_sleep,
					 (q->qlen > 0 || q->head == q->tail),
					 q->pre_event_timeout);
}

/*
 * drain one record
 * caller must hold lock
 */
void
snd_seq_oss_readq_free(seq_oss_readq_t *q)
{
	if (q->qlen > 0) {
		q->head = (q->head + 1) % q->maxlen;
		q->qlen--;
	}
}

/*
 * polling/select:
 * return non-zero if readq is not empty.
 */
unsigned int
snd_seq_oss_readq_poll(seq_oss_readq_t *q, struct file *file, poll_table *wait)
{
	poll_wait(file, &q->midi_sleep, wait);
	return q->qlen;
}

/*
 * put a timestamp
 */
int
snd_seq_oss_readq_put_timestamp(seq_oss_readq_t *q, unsigned long curt, int seq_mode)
{
	if (curt != q->input_time) {
		evrec_t rec;
		memset(&rec, 0, sizeof(rec));
		switch (seq_mode) {
		case SNDRV_SEQ_OSS_MODE_SYNTH:
			rec.echo = (curt << 8) | SEQ_WAIT;
			snd_seq_oss_readq_put_event(q, &rec);
			break;
		case SNDRV_SEQ_OSS_MODE_MUSIC:
			rec.t.code = EV_TIMING;
			rec.t.cmd = TMR_WAIT_ABS;
			rec.t.time = curt;
			snd_seq_oss_readq_put_event(q, &rec);
			break;
		}
		q->input_time = curt;
	}
	return 0;
}


/*
 * proc interface
 */
void
snd_seq_oss_readq_info_read(seq_oss_readq_t *q, snd_info_buffer_t *buf)
{
	snd_iprintf(buf, "  read queue [%s] length = %d : tick = %ld\n",
		    (waitqueue_active(&q->midi_sleep) ? "sleeping":"running"),
		    q->qlen, q->input_time);
}
