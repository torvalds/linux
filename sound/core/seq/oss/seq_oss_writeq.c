/*
 * OSS compatible sequencer driver
 *
 * seq_oss_writeq.c - write queue and sync
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

#include "seq_oss_writeq.h"
#include "seq_oss_event.h"
#include "seq_oss_timer.h"
#include <sound/seq_oss_legacy.h>
#include "../seq_lock.h"
#include "../seq_clientmgr.h"
#include <linux/wait.h>
#include <linux/slab.h>


/*
 * create a write queue record
 */
struct seq_oss_writeq *
snd_seq_oss_writeq_new(struct seq_oss_devinfo *dp, int maxlen)
{
	struct seq_oss_writeq *q;
	struct snd_seq_client_pool pool;

	if ((q = kzalloc(sizeof(*q), GFP_KERNEL)) == NULL)
		return NULL;
	q->dp = dp;
	q->maxlen = maxlen;
	spin_lock_init(&q->sync_lock);
	q->sync_event_put = 0;
	q->sync_time = 0;
	init_waitqueue_head(&q->sync_sleep);

	memset(&pool, 0, sizeof(pool));
	pool.client = dp->cseq;
	pool.output_pool = maxlen;
	pool.output_room = maxlen / 2;

	snd_seq_oss_control(dp, SNDRV_SEQ_IOCTL_SET_CLIENT_POOL, &pool);

	return q;
}

/*
 * delete the write queue
 */
void
snd_seq_oss_writeq_delete(struct seq_oss_writeq *q)
{
	if (q) {
		snd_seq_oss_writeq_clear(q);	/* to be sure */
		kfree(q);
	}
}


/*
 * reset the write queue
 */
void
snd_seq_oss_writeq_clear(struct seq_oss_writeq *q)
{
	struct snd_seq_remove_events reset;

	memset(&reset, 0, sizeof(reset));
	reset.remove_mode = SNDRV_SEQ_REMOVE_OUTPUT; /* remove all */
	snd_seq_oss_control(q->dp, SNDRV_SEQ_IOCTL_REMOVE_EVENTS, &reset);

	/* wake up sleepers if any */
	snd_seq_oss_writeq_wakeup(q, 0);
}

/*
 * wait until the write buffer has enough room
 */
int
snd_seq_oss_writeq_sync(struct seq_oss_writeq *q)
{
	struct seq_oss_devinfo *dp = q->dp;
	abstime_t time;

	time = snd_seq_oss_timer_cur_tick(dp->timer);
	if (q->sync_time >= time)
		return 0; /* already finished */

	if (! q->sync_event_put) {
		struct snd_seq_event ev;
		union evrec *rec;

		/* put echoback event */
		memset(&ev, 0, sizeof(ev));
		ev.flags = 0;
		ev.type = SNDRV_SEQ_EVENT_ECHO;
		ev.time.tick = time;
		/* echo back to itself */
		snd_seq_oss_fill_addr(dp, &ev, dp->addr.client, dp->addr.port);
		rec = (union evrec *)&ev.data;
		rec->t.code = SEQ_SYNCTIMER;
		rec->t.time = time;
		q->sync_event_put = 1;
		snd_seq_kernel_client_enqueue_blocking(dp->cseq, &ev, NULL, 0, 0);
	}

	wait_event_interruptible_timeout(q->sync_sleep, ! q->sync_event_put, HZ);
	if (signal_pending(current))
		/* interrupted - return 0 to finish sync */
		q->sync_event_put = 0;
	if (! q->sync_event_put || q->sync_time >= time)
		return 0;
	return 1;
}

/*
 * wake up sync - echo event was catched
 */
void
snd_seq_oss_writeq_wakeup(struct seq_oss_writeq *q, abstime_t time)
{
	unsigned long flags;

	spin_lock_irqsave(&q->sync_lock, flags);
	q->sync_time = time;
	q->sync_event_put = 0;
	wake_up(&q->sync_sleep);
	spin_unlock_irqrestore(&q->sync_lock, flags);
}


/*
 * return the unused pool size
 */
int
snd_seq_oss_writeq_get_free_size(struct seq_oss_writeq *q)
{
	struct snd_seq_client_pool pool;
	pool.client = q->dp->cseq;
	snd_seq_oss_control(q->dp, SNDRV_SEQ_IOCTL_GET_CLIENT_POOL, &pool);
	return pool.output_free;
}


/*
 * set output threshold size from ioctl
 */
void
snd_seq_oss_writeq_set_output(struct seq_oss_writeq *q, int val)
{
	struct snd_seq_client_pool pool;
	pool.client = q->dp->cseq;
	snd_seq_oss_control(q->dp, SNDRV_SEQ_IOCTL_GET_CLIENT_POOL, &pool);
	pool.output_room = val;
	snd_seq_oss_control(q->dp, SNDRV_SEQ_IOCTL_SET_CLIENT_POOL, &pool);
}

