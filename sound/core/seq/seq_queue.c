// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   ALSA sequencer Timing queue handling
 *   Copyright (c) 1998-1999 by Frank van de Pol <fvdpol@coil.demon.nl>
 *
 * MAJOR CHANGES
 *   Nov. 13, 1999	Takashi Iwai <iwai@ww.uni-erlangen.de>
 *     - Queues are allocated dynamically via ioctl.
 *     - When owner client is deleted, all owned queues are deleted, too.
 *     - Owner of unlocked queue is kept unmodified even if it is
 *	 manipulated by other clients.
 *     - Owner field in SET_QUEUE_OWNER ioctl must be identical with the
 *       caller client.  i.e. Changing owner to a third client is not
 *       allowed.
 *
 *  Aug. 30, 2000	Takashi Iwai
 *     - Queues are managed in static array again, but with better way.
 *       The API itself is identical.
 *     - The queue is locked when struct snd_seq_queue pointer is returned via
 *       queueptr().  This pointer *MUST* be released afterward by
 *       queuefree(ptr).
 *     - Addition of experimental sync support.
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <sound/core.h>

#include "seq_memory.h"
#include "seq_queue.h"
#include "seq_clientmgr.h"
#include "seq_fifo.h"
#include "seq_timer.h"
#include "seq_info.h"

/* list of allocated queues */
static struct snd_seq_queue *queue_list[SNDRV_SEQ_MAX_QUEUES];
static DEFINE_SPINLOCK(queue_list_lock);
/* number of queues allocated */
static int num_queues;

int snd_seq_queue_get_cur_queues(void)
{
	return num_queues;
}

/*----------------------------------------------------------------*/

/* assign queue id and insert to list */
static int queue_list_add(struct snd_seq_queue *q)
{
	int i;
	unsigned long flags;

	spin_lock_irqsave(&queue_list_lock, flags);
	for (i = 0; i < SNDRV_SEQ_MAX_QUEUES; i++) {
		if (! queue_list[i]) {
			queue_list[i] = q;
			q->queue = i;
			num_queues++;
			spin_unlock_irqrestore(&queue_list_lock, flags);
			return i;
		}
	}
	spin_unlock_irqrestore(&queue_list_lock, flags);
	return -1;
}

static struct snd_seq_queue *queue_list_remove(int id, int client)
{
	struct snd_seq_queue *q;
	unsigned long flags;

	spin_lock_irqsave(&queue_list_lock, flags);
	q = queue_list[id];
	if (q) {
		spin_lock(&q->owner_lock);
		if (q->owner == client) {
			/* found */
			q->klocked = 1;
			spin_unlock(&q->owner_lock);
			queue_list[id] = NULL;
			num_queues--;
			spin_unlock_irqrestore(&queue_list_lock, flags);
			return q;
		}
		spin_unlock(&q->owner_lock);
	}
	spin_unlock_irqrestore(&queue_list_lock, flags);
	return NULL;
}

/*----------------------------------------------------------------*/

/* create new queue (constructor) */
static struct snd_seq_queue *queue_new(int owner, int locked)
{
	struct snd_seq_queue *q;

	q = kzalloc(sizeof(*q), GFP_KERNEL);
	if (!q)
		return NULL;

	spin_lock_init(&q->owner_lock);
	spin_lock_init(&q->check_lock);
	mutex_init(&q->timer_mutex);
	snd_use_lock_init(&q->use_lock);
	q->queue = -1;

	q->tickq = snd_seq_prioq_new();
	q->timeq = snd_seq_prioq_new();
	q->timer = snd_seq_timer_new();
	if (q->tickq == NULL || q->timeq == NULL || q->timer == NULL) {
		snd_seq_prioq_delete(&q->tickq);
		snd_seq_prioq_delete(&q->timeq);
		snd_seq_timer_delete(&q->timer);
		kfree(q);
		return NULL;
	}

	q->owner = owner;
	q->locked = locked;
	q->klocked = 0;

	return q;
}

/* delete queue (destructor) */
static void queue_delete(struct snd_seq_queue *q)
{
	/* stop and release the timer */
	mutex_lock(&q->timer_mutex);
	snd_seq_timer_stop(q->timer);
	snd_seq_timer_close(q);
	mutex_unlock(&q->timer_mutex);
	/* wait until access free */
	snd_use_lock_sync(&q->use_lock);
	/* release resources... */
	snd_seq_prioq_delete(&q->tickq);
	snd_seq_prioq_delete(&q->timeq);
	snd_seq_timer_delete(&q->timer);

	kfree(q);
}


/*----------------------------------------------------------------*/

/* delete all existing queues */
void snd_seq_queues_delete(void)
{
	int i;

	/* clear list */
	for (i = 0; i < SNDRV_SEQ_MAX_QUEUES; i++) {
		if (queue_list[i])
			queue_delete(queue_list[i]);
	}
}

static void queue_use(struct snd_seq_queue *queue, int client, int use);

/* allocate a new queue -
 * return pointer to new queue or ERR_PTR(-errno) for error
 * The new queue's use_lock is set to 1. It is the caller's responsibility to
 * call snd_use_lock_free(&q->use_lock).
 */
struct snd_seq_queue *snd_seq_queue_alloc(int client, int locked, unsigned int info_flags)
{
	struct snd_seq_queue *q;

	q = queue_new(client, locked);
	if (q == NULL)
		return ERR_PTR(-ENOMEM);
	q->info_flags = info_flags;
	queue_use(q, client, 1);
	snd_use_lock_use(&q->use_lock);
	if (queue_list_add(q) < 0) {
		snd_use_lock_free(&q->use_lock);
		queue_delete(q);
		return ERR_PTR(-ENOMEM);
	}
	return q;
}

/* delete a queue - queue must be owned by the client */
int snd_seq_queue_delete(int client, int queueid)
{
	struct snd_seq_queue *q;

	if (queueid < 0 || queueid >= SNDRV_SEQ_MAX_QUEUES)
		return -EINVAL;
	q = queue_list_remove(queueid, client);
	if (q == NULL)
		return -EINVAL;
	queue_delete(q);

	return 0;
}


/* return pointer to queue structure for specified id */
struct snd_seq_queue *queueptr(int queueid)
{
	struct snd_seq_queue *q;
	unsigned long flags;

	if (queueid < 0 || queueid >= SNDRV_SEQ_MAX_QUEUES)
		return NULL;
	spin_lock_irqsave(&queue_list_lock, flags);
	q = queue_list[queueid];
	if (q)
		snd_use_lock_use(&q->use_lock);
	spin_unlock_irqrestore(&queue_list_lock, flags);
	return q;
}

/* return the (first) queue matching with the specified name */
struct snd_seq_queue *snd_seq_queue_find_name(char *name)
{
	int i;
	struct snd_seq_queue *q;

	for (i = 0; i < SNDRV_SEQ_MAX_QUEUES; i++) {
		if ((q = queueptr(i)) != NULL) {
			if (strncmp(q->name, name, sizeof(q->name)) == 0)
				return q;
			queuefree(q);
		}
	}
	return NULL;
}


/* -------------------------------------------------------- */

void snd_seq_check_queue(struct snd_seq_queue *q, int atomic, int hop)
{
	unsigned long flags;
	struct snd_seq_event_cell *cell;

	if (q == NULL)
		return;

	/* make this function non-reentrant */
	spin_lock_irqsave(&q->check_lock, flags);
	if (q->check_blocked) {
		q->check_again = 1;
		spin_unlock_irqrestore(&q->check_lock, flags);
		return;		/* other thread is already checking queues */
	}
	q->check_blocked = 1;
	spin_unlock_irqrestore(&q->check_lock, flags);

      __again:
	/* Process tick queue... */
	for (;;) {
		cell = snd_seq_prioq_cell_out(q->tickq,
					      &q->timer->tick.cur_tick);
		if (!cell)
			break;
		snd_seq_dispatch_event(cell, atomic, hop);
	}

	/* Process time queue... */
	for (;;) {
		cell = snd_seq_prioq_cell_out(q->timeq, &q->timer->cur_time);
		if (!cell)
			break;
		snd_seq_dispatch_event(cell, atomic, hop);
	}

	/* free lock */
	spin_lock_irqsave(&q->check_lock, flags);
	if (q->check_again) {
		q->check_again = 0;
		spin_unlock_irqrestore(&q->check_lock, flags);
		goto __again;
	}
	q->check_blocked = 0;
	spin_unlock_irqrestore(&q->check_lock, flags);
}


/* enqueue a event to singe queue */
int snd_seq_enqueue_event(struct snd_seq_event_cell *cell, int atomic, int hop)
{
	int dest, err;
	struct snd_seq_queue *q;

	if (snd_BUG_ON(!cell))
		return -EINVAL;
	dest = cell->event.queue;	/* destination queue */
	q = queueptr(dest);
	if (q == NULL)
		return -EINVAL;
	/* handle relative time stamps, convert them into absolute */
	if ((cell->event.flags & SNDRV_SEQ_TIME_MODE_MASK) == SNDRV_SEQ_TIME_MODE_REL) {
		switch (cell->event.flags & SNDRV_SEQ_TIME_STAMP_MASK) {
		case SNDRV_SEQ_TIME_STAMP_TICK:
			cell->event.time.tick += q->timer->tick.cur_tick;
			break;

		case SNDRV_SEQ_TIME_STAMP_REAL:
			snd_seq_inc_real_time(&cell->event.time.time,
					      &q->timer->cur_time);
			break;
		}
		cell->event.flags &= ~SNDRV_SEQ_TIME_MODE_MASK;
		cell->event.flags |= SNDRV_SEQ_TIME_MODE_ABS;
	}
	/* enqueue event in the real-time or midi queue */
	switch (cell->event.flags & SNDRV_SEQ_TIME_STAMP_MASK) {
	case SNDRV_SEQ_TIME_STAMP_TICK:
		err = snd_seq_prioq_cell_in(q->tickq, cell);
		break;

	case SNDRV_SEQ_TIME_STAMP_REAL:
	default:
		err = snd_seq_prioq_cell_in(q->timeq, cell);
		break;
	}

	if (err < 0) {
		queuefree(q); /* unlock */
		return err;
	}

	/* trigger dispatching */
	snd_seq_check_queue(q, atomic, hop);

	queuefree(q); /* unlock */

	return 0;
}


/*----------------------------------------------------------------*/

static inline int check_access(struct snd_seq_queue *q, int client)
{
	return (q->owner == client) || (!q->locked && !q->klocked);
}

/* check if the client has permission to modify queue parameters.
 * if it does, lock the queue
 */
static int queue_access_lock(struct snd_seq_queue *q, int client)
{
	unsigned long flags;
	int access_ok;
	
	spin_lock_irqsave(&q->owner_lock, flags);
	access_ok = check_access(q, client);
	if (access_ok)
		q->klocked = 1;
	spin_unlock_irqrestore(&q->owner_lock, flags);
	return access_ok;
}

/* unlock the queue */
static inline void queue_access_unlock(struct snd_seq_queue *q)
{
	unsigned long flags;

	spin_lock_irqsave(&q->owner_lock, flags);
	q->klocked = 0;
	spin_unlock_irqrestore(&q->owner_lock, flags);
}

/* exported - only checking permission */
int snd_seq_queue_check_access(int queueid, int client)
{
	struct snd_seq_queue *q = queueptr(queueid);
	int access_ok;
	unsigned long flags;

	if (! q)
		return 0;
	spin_lock_irqsave(&q->owner_lock, flags);
	access_ok = check_access(q, client);
	spin_unlock_irqrestore(&q->owner_lock, flags);
	queuefree(q);
	return access_ok;
}

/*----------------------------------------------------------------*/

/*
 * change queue's owner and permission
 */
int snd_seq_queue_set_owner(int queueid, int client, int locked)
{
	struct snd_seq_queue *q = queueptr(queueid);
	unsigned long flags;

	if (q == NULL)
		return -EINVAL;

	if (! queue_access_lock(q, client)) {
		queuefree(q);
		return -EPERM;
	}

	spin_lock_irqsave(&q->owner_lock, flags);
	q->locked = locked ? 1 : 0;
	q->owner = client;
	spin_unlock_irqrestore(&q->owner_lock, flags);
	queue_access_unlock(q);
	queuefree(q);

	return 0;
}


/*----------------------------------------------------------------*/

/* open timer -
 * q->use mutex should be down before calling this function to avoid
 * confliction with snd_seq_queue_use()
 */
int snd_seq_queue_timer_open(int queueid)
{
	int result = 0;
	struct snd_seq_queue *queue;
	struct snd_seq_timer *tmr;

	queue = queueptr(queueid);
	if (queue == NULL)
		return -EINVAL;
	tmr = queue->timer;
	if ((result = snd_seq_timer_open(queue)) < 0) {
		snd_seq_timer_defaults(tmr);
		result = snd_seq_timer_open(queue);
	}
	queuefree(queue);
	return result;
}

/* close timer -
 * q->use mutex should be down before calling this function
 */
int snd_seq_queue_timer_close(int queueid)
{
	struct snd_seq_queue *queue;
	int result = 0;

	queue = queueptr(queueid);
	if (queue == NULL)
		return -EINVAL;
	snd_seq_timer_close(queue);
	queuefree(queue);
	return result;
}

/* change queue tempo and ppq */
int snd_seq_queue_timer_set_tempo(int queueid, int client,
				  struct snd_seq_queue_tempo *info)
{
	struct snd_seq_queue *q = queueptr(queueid);
	int result;

	if (q == NULL)
		return -EINVAL;
	if (! queue_access_lock(q, client)) {
		queuefree(q);
		return -EPERM;
	}

	result = snd_seq_timer_set_tempo_ppq(q->timer, info->tempo, info->ppq);
	if (result >= 0 && info->skew_base > 0)
		result = snd_seq_timer_set_skew(q->timer, info->skew_value,
						info->skew_base);
	queue_access_unlock(q);
	queuefree(q);
	return result;
}

/* use or unuse this queue */
static void queue_use(struct snd_seq_queue *queue, int client, int use)
{
	if (use) {
		if (!test_and_set_bit(client, queue->clients_bitmap))
			queue->clients++;
	} else {
		if (test_and_clear_bit(client, queue->clients_bitmap))
			queue->clients--;
	}
	if (queue->clients) {
		if (use && queue->clients == 1)
			snd_seq_timer_defaults(queue->timer);
		snd_seq_timer_open(queue);
	} else {
		snd_seq_timer_close(queue);
	}
}

/* use or unuse this queue -
 * if it is the first client, starts the timer.
 * if it is not longer used by any clients, stop the timer.
 */
int snd_seq_queue_use(int queueid, int client, int use)
{
	struct snd_seq_queue *queue;

	queue = queueptr(queueid);
	if (queue == NULL)
		return -EINVAL;
	mutex_lock(&queue->timer_mutex);
	queue_use(queue, client, use);
	mutex_unlock(&queue->timer_mutex);
	queuefree(queue);
	return 0;
}

/*
 * check if queue is used by the client
 * return negative value if the queue is invalid.
 * return 0 if not used, 1 if used.
 */
int snd_seq_queue_is_used(int queueid, int client)
{
	struct snd_seq_queue *q;
	int result;

	q = queueptr(queueid);
	if (q == NULL)
		return -EINVAL; /* invalid queue */
	result = test_bit(client, q->clients_bitmap) ? 1 : 0;
	queuefree(q);
	return result;
}


/*----------------------------------------------------------------*/

/* notification that client has left the system -
 * stop the timer on all queues owned by this client
 */
void snd_seq_queue_client_termination(int client)
{
	unsigned long flags;
	int i;
	struct snd_seq_queue *q;
	bool matched;

	for (i = 0; i < SNDRV_SEQ_MAX_QUEUES; i++) {
		if ((q = queueptr(i)) == NULL)
			continue;
		spin_lock_irqsave(&q->owner_lock, flags);
		matched = (q->owner == client);
		if (matched)
			q->klocked = 1;
		spin_unlock_irqrestore(&q->owner_lock, flags);
		if (matched) {
			if (q->timer->running)
				snd_seq_timer_stop(q->timer);
			snd_seq_timer_reset(q->timer);
		}
		queuefree(q);
	}
}

/* final stage notification -
 * remove cells for no longer exist client (for non-owned queue)
 * or delete this queue (for owned queue)
 */
void snd_seq_queue_client_leave(int client)
{
	int i;
	struct snd_seq_queue *q;

	/* delete own queues from queue list */
	for (i = 0; i < SNDRV_SEQ_MAX_QUEUES; i++) {
		if ((q = queue_list_remove(i, client)) != NULL)
			queue_delete(q);
	}

	/* remove cells from existing queues -
	 * they are not owned by this client
	 */
	for (i = 0; i < SNDRV_SEQ_MAX_QUEUES; i++) {
		if ((q = queueptr(i)) == NULL)
			continue;
		if (test_bit(client, q->clients_bitmap)) {
			snd_seq_prioq_leave(q->tickq, client, 0);
			snd_seq_prioq_leave(q->timeq, client, 0);
			snd_seq_queue_use(q->queue, client, 0);
		}
		queuefree(q);
	}
}



/*----------------------------------------------------------------*/

/* remove cells from all queues */
void snd_seq_queue_client_leave_cells(int client)
{
	int i;
	struct snd_seq_queue *q;

	for (i = 0; i < SNDRV_SEQ_MAX_QUEUES; i++) {
		if ((q = queueptr(i)) == NULL)
			continue;
		snd_seq_prioq_leave(q->tickq, client, 0);
		snd_seq_prioq_leave(q->timeq, client, 0);
		queuefree(q);
	}
}

/* remove cells based on flush criteria */
void snd_seq_queue_remove_cells(int client, struct snd_seq_remove_events *info)
{
	int i;
	struct snd_seq_queue *q;

	for (i = 0; i < SNDRV_SEQ_MAX_QUEUES; i++) {
		if ((q = queueptr(i)) == NULL)
			continue;
		if (test_bit(client, q->clients_bitmap) &&
		    (! (info->remove_mode & SNDRV_SEQ_REMOVE_DEST) ||
		     q->queue == info->queue)) {
			snd_seq_prioq_remove_events(q->tickq, client, info);
			snd_seq_prioq_remove_events(q->timeq, client, info);
		}
		queuefree(q);
	}
}

/*----------------------------------------------------------------*/

/*
 * send events to all subscribed ports
 */
static void queue_broadcast_event(struct snd_seq_queue *q, struct snd_seq_event *ev,
				  int atomic, int hop)
{
	struct snd_seq_event sev;

	sev = *ev;
	
	sev.flags = SNDRV_SEQ_TIME_STAMP_TICK|SNDRV_SEQ_TIME_MODE_ABS;
	sev.time.tick = q->timer->tick.cur_tick;
	sev.queue = q->queue;
	sev.data.queue.queue = q->queue;

	/* broadcast events from Timer port */
	sev.source.client = SNDRV_SEQ_CLIENT_SYSTEM;
	sev.source.port = SNDRV_SEQ_PORT_SYSTEM_TIMER;
	sev.dest.client = SNDRV_SEQ_ADDRESS_SUBSCRIBERS;
	snd_seq_kernel_client_dispatch(SNDRV_SEQ_CLIENT_SYSTEM, &sev, atomic, hop);
}

/*
 * process a received queue-control event.
 * this function is exported for seq_sync.c.
 */
static void snd_seq_queue_process_event(struct snd_seq_queue *q,
					struct snd_seq_event *ev,
					int atomic, int hop)
{
	switch (ev->type) {
	case SNDRV_SEQ_EVENT_START:
		snd_seq_prioq_leave(q->tickq, ev->source.client, 1);
		snd_seq_prioq_leave(q->timeq, ev->source.client, 1);
		if (! snd_seq_timer_start(q->timer))
			queue_broadcast_event(q, ev, atomic, hop);
		break;

	case SNDRV_SEQ_EVENT_CONTINUE:
		if (! snd_seq_timer_continue(q->timer))
			queue_broadcast_event(q, ev, atomic, hop);
		break;

	case SNDRV_SEQ_EVENT_STOP:
		snd_seq_timer_stop(q->timer);
		queue_broadcast_event(q, ev, atomic, hop);
		break;

	case SNDRV_SEQ_EVENT_TEMPO:
		snd_seq_timer_set_tempo(q->timer, ev->data.queue.param.value);
		queue_broadcast_event(q, ev, atomic, hop);
		break;

	case SNDRV_SEQ_EVENT_SETPOS_TICK:
		if (snd_seq_timer_set_position_tick(q->timer, ev->data.queue.param.time.tick) == 0) {
			queue_broadcast_event(q, ev, atomic, hop);
		}
		break;

	case SNDRV_SEQ_EVENT_SETPOS_TIME:
		if (snd_seq_timer_set_position_time(q->timer, ev->data.queue.param.time.time) == 0) {
			queue_broadcast_event(q, ev, atomic, hop);
		}
		break;
	case SNDRV_SEQ_EVENT_QUEUE_SKEW:
		if (snd_seq_timer_set_skew(q->timer,
					   ev->data.queue.param.skew.value,
					   ev->data.queue.param.skew.base) == 0) {
			queue_broadcast_event(q, ev, atomic, hop);
		}
		break;
	}
}


/*
 * Queue control via timer control port:
 * this function is exported as a callback of timer port.
 */
int snd_seq_control_queue(struct snd_seq_event *ev, int atomic, int hop)
{
	struct snd_seq_queue *q;

	if (snd_BUG_ON(!ev))
		return -EINVAL;
	q = queueptr(ev->data.queue.queue);

	if (q == NULL)
		return -EINVAL;

	if (! queue_access_lock(q, ev->source.client)) {
		queuefree(q);
		return -EPERM;
	}

	snd_seq_queue_process_event(q, ev, atomic, hop);

	queue_access_unlock(q);
	queuefree(q);
	return 0;
}


/*----------------------------------------------------------------*/

#ifdef CONFIG_SND_PROC_FS
/* exported to seq_info.c */
void snd_seq_info_queues_read(struct snd_info_entry *entry, 
			      struct snd_info_buffer *buffer)
{
	int i, bpm;
	struct snd_seq_queue *q;
	struct snd_seq_timer *tmr;
	bool locked;
	int owner;

	for (i = 0; i < SNDRV_SEQ_MAX_QUEUES; i++) {
		if ((q = queueptr(i)) == NULL)
			continue;

		tmr = q->timer;
		if (tmr->tempo)
			bpm = 60000000 / tmr->tempo;
		else
			bpm = 0;

		spin_lock_irq(&q->owner_lock);
		locked = q->locked;
		owner = q->owner;
		spin_unlock_irq(&q->owner_lock);

		snd_iprintf(buffer, "queue %d: [%s]\n", q->queue, q->name);
		snd_iprintf(buffer, "owned by client    : %d\n", owner);
		snd_iprintf(buffer, "lock status        : %s\n", locked ? "Locked" : "Free");
		snd_iprintf(buffer, "queued time events : %d\n", snd_seq_prioq_avail(q->timeq));
		snd_iprintf(buffer, "queued tick events : %d\n", snd_seq_prioq_avail(q->tickq));
		snd_iprintf(buffer, "timer state        : %s\n", tmr->running ? "Running" : "Stopped");
		snd_iprintf(buffer, "timer PPQ          : %d\n", tmr->ppq);
		snd_iprintf(buffer, "current tempo      : %d\n", tmr->tempo);
		snd_iprintf(buffer, "current BPM        : %d\n", bpm);
		snd_iprintf(buffer, "current time       : %d.%09d s\n", tmr->cur_time.tv_sec, tmr->cur_time.tv_nsec);
		snd_iprintf(buffer, "current tick       : %d\n", tmr->tick.cur_tick);
		snd_iprintf(buffer, "\n");
		queuefree(q);
	}
}
#endif /* CONFIG_SND_PROC_FS */

