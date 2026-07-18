// SPDX-License-Identifier: GPL-2.0-only

#include "dev.h"
#include "sysctl.h"
#include "fuse_dev_i.h"
#include "dev_uring_i.h"

/* Frequency (in seconds) of request timeout checks, if opted into */
#define FUSE_TIMEOUT_TIMER_FREQ 15

/* Frequency (in jiffies) of request timeout checks, if opted into */
static const unsigned long fuse_timeout_timer_freq =
	secs_to_jiffies(FUSE_TIMEOUT_TIMER_FREQ);

/*
 * Default timeout (in seconds) for the server to reply to a request
 * before the connection is aborted, if no timeout was specified on mount.
 *
 * Exported via sysctl
 */
unsigned int fuse_default_req_timeout;

/*
 * Max timeout (in seconds) for the server to reply to a request before
 * the connection is aborted.
 *
 * Exported via sysctl
 */
unsigned int fuse_max_req_timeout;

bool fuse_request_expired(struct fuse_chan *fch, struct list_head *list)
{
	struct fuse_req *req;

	req = list_first_entry_or_null(list, struct fuse_req, list);
	if (!req)
		return false;
	return time_is_before_jiffies(req->create_time + fch->timeout.req_timeout);
}

static bool fuse_fpq_processing_expired(struct fuse_chan *fch, struct list_head *processing)
{
	int i;

	for (i = 0; i < FUSE_PQ_HASH_SIZE; i++)
		if (fuse_request_expired(fch, &processing[i]))
			return true;

	return false;
}

/*
 * Check if any requests aren't being completed by the time the request timeout
 * elapses. To do so, we:
 * - check the fiq pending list
 * - check the bg queue
 * - check the fpq io and processing lists
 *
 * To make this fast, we only check against the head request on each list since
 * these are generally queued in order of creation time (eg newer requests get
 * queued to the tail). We might miss a few edge cases (eg requests transitioning
 * between lists, re-sent requests at the head of the pending list having a
 * later creation time than other requests on that list, etc.) but that is fine
 * since if the request never gets fulfilled, it will eventually be caught.
 */
static void fuse_check_timeout(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct fuse_chan *fch = container_of(dwork, struct fuse_chan, timeout.work);
	struct fuse_iqueue *fiq = &fch->iq;
	struct fuse_dev *fud;
	struct fuse_pqueue *fpq;
	bool expired = false;

	if (!atomic_read(&fch->num_waiting))
		goto out;

	spin_lock(&fiq->lock);
	expired = fuse_request_expired(fch, &fiq->pending);
	spin_unlock(&fiq->lock);
	if (expired)
		goto chan_abort;

	spin_lock(&fch->bg_lock);
	expired = fuse_request_expired(fch, &fch->bg_queue);
	spin_unlock(&fch->bg_lock);
	if (expired)
		goto chan_abort;

	spin_lock(&fch->lock);
	if (!fch->connected) {
		spin_unlock(&fch->lock);
		return;
	}
	list_for_each_entry(fud, &fch->devices, entry) {
		fpq = &fud->pq;
		spin_lock(&fpq->lock);
		if (fuse_request_expired(fch, &fpq->io) ||
		    fuse_fpq_processing_expired(fch, fpq->processing)) {
			spin_unlock(&fpq->lock);
			spin_unlock(&fch->lock);
			goto chan_abort;
		}

		spin_unlock(&fpq->lock);
	}
	spin_unlock(&fch->lock);

	if (fuse_uring_request_expired(fch))
		goto chan_abort;

out:
	queue_delayed_work(system_percpu_wq, &fch->timeout.work,
			   fuse_timeout_timer_freq);
	return;

chan_abort:
	fuse_chan_abort(fch, false);
}

static void set_request_timeout(struct fuse_chan *fch, unsigned int timeout)
{
	fch->timeout.req_timeout = secs_to_jiffies(timeout);
	INIT_DELAYED_WORK(&fch->timeout.work, fuse_check_timeout);
	queue_delayed_work(system_percpu_wq, &fch->timeout.work,
			   fuse_timeout_timer_freq);
}

void fuse_init_server_timeout(struct fuse_chan *fch, unsigned int timeout)
{
	if (!timeout && !fuse_max_req_timeout && !fuse_default_req_timeout)
		return;

	if (!timeout)
		timeout = fuse_default_req_timeout;

	if (fuse_max_req_timeout) {
		if (timeout)
			timeout = min(fuse_max_req_timeout, timeout);
		else
			timeout = fuse_max_req_timeout;
	}

	timeout = max(FUSE_TIMEOUT_TIMER_FREQ, timeout);

	set_request_timeout(fch, timeout);
}

