/*
 * net/9p/mux.c
 *
 * Protocol Multiplexer
 *
 *  Copyright (C) 2004 by Eric Van Hensbergen <ericvh@gmail.com>
 *  Copyright (C) 2004-2005 by Latchesar Ionkov <lucho@ionkov.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2
 *  as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to:
 *  Free Software Foundation
 *  51 Franklin Street, Fifth Floor
 *  Boston, MA  02111-1301  USA
 *
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/kthread.h>
#include <linux/idr.h>
#include <linux/mutex.h>
#include <net/9p/9p.h>
#include <net/9p/transport.h>
#include <net/9p/conn.h>

#define ERREQFLUSH	1
#define SCHED_TIMEOUT	10
#define MAXPOLLWADDR	2

enum {
	Rworksched = 1,		/* read work scheduled or running */
	Rpending = 2,		/* can read */
	Wworksched = 4,		/* write work scheduled or running */
	Wpending = 8,		/* can write */
};

enum {
	None,
	Flushing,
	Flushed,
};

struct p9_mux_poll_task;

struct p9_req {
	spinlock_t lock; /* protect request structure */
	int tag;
	struct p9_fcall *tcall;
	struct p9_fcall *rcall;
	int err;
	p9_conn_req_callback cb;
	void *cba;
	int flush;
	struct list_head req_list;
};

struct p9_conn {
	spinlock_t lock; /* protect lock structure */
	struct list_head mux_list;
	struct p9_mux_poll_task *poll_task;
	int msize;
	unsigned char *extended;
	struct p9_transport *trans;
	struct p9_idpool *tagpool;
	int err;
	wait_queue_head_t equeue;
	struct list_head req_list;
	struct list_head unsent_req_list;
	struct p9_fcall *rcall;
	int rpos;
	char *rbuf;
	int wpos;
	int wsize;
	char *wbuf;
	wait_queue_t poll_wait[MAXPOLLWADDR];
	wait_queue_head_t *poll_waddr[MAXPOLLWADDR];
	poll_table pt;
	struct work_struct rq;
	struct work_struct wq;
	unsigned long wsched;
};

struct p9_mux_poll_task {
	struct task_struct *task;
	struct list_head mux_list;
	int muxnum;
};

struct p9_mux_rpc {
	struct p9_conn *m;
	int err;
	struct p9_fcall *tcall;
	struct p9_fcall *rcall;
	wait_queue_head_t wqueue;
};

static int p9_poll_proc(void *);
static void p9_read_work(struct work_struct *work);
static void p9_write_work(struct work_struct *work);
static void p9_pollwait(struct file *filp, wait_queue_head_t *wait_address,
			  poll_table * p);
static u16 p9_mux_get_tag(struct p9_conn *);
static void p9_mux_put_tag(struct p9_conn *, u16);

static DEFINE_MUTEX(p9_mux_task_lock);
static struct workqueue_struct *p9_mux_wq;

static int p9_mux_num;
static int p9_mux_poll_task_num;
static struct p9_mux_poll_task p9_mux_poll_tasks[100];

int p9_mux_global_init(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(p9_mux_poll_tasks); i++)
		p9_mux_poll_tasks[i].task = NULL;

	p9_mux_wq = create_workqueue("v9fs");
	if (!p9_mux_wq) {
		printk(KERN_WARNING "v9fs: mux: creating workqueue failed\n");
		return -ENOMEM;
	}

	return 0;
}

void p9_mux_global_exit(void)
{
	destroy_workqueue(p9_mux_wq);
}

/**
 * p9_mux_calc_poll_procs - calculates the number of polling procs
 * based on the number of mounted v9fs filesystems.
 *
 * The current implementation returns sqrt of the number of mounts.
 */
static int p9_mux_calc_poll_procs(int muxnum)
{
	int n;

	if (p9_mux_poll_task_num)
		n = muxnum / p9_mux_poll_task_num +
		    (muxnum % p9_mux_poll_task_num ? 1 : 0);
	else
		n = 1;

	if (n > ARRAY_SIZE(p9_mux_poll_tasks))
		n = ARRAY_SIZE(p9_mux_poll_tasks);

	return n;
}

static int p9_mux_poll_start(struct p9_conn *m)
{
	int i, n;
	struct p9_mux_poll_task *vpt, *vptlast;
	struct task_struct *pproc;

	P9_DPRINTK(P9_DEBUG_MUX, "mux %p muxnum %d procnum %d\n", m, p9_mux_num,
		p9_mux_poll_task_num);
	mutex_lock(&p9_mux_task_lock);

	n = p9_mux_calc_poll_procs(p9_mux_num + 1);
	if (n > p9_mux_poll_task_num) {
		for (i = 0; i < ARRAY_SIZE(p9_mux_poll_tasks); i++) {
			if (p9_mux_poll_tasks[i].task == NULL) {
				vpt = &p9_mux_poll_tasks[i];
				P9_DPRINTK(P9_DEBUG_MUX, "create proc %p\n",
									vpt);
				pproc = kthread_create(p9_poll_proc, vpt,
								"v9fs-poll");

				if (!IS_ERR(pproc)) {
					vpt->task = pproc;
					INIT_LIST_HEAD(&vpt->mux_list);
					vpt->muxnum = 0;
					p9_mux_poll_task_num++;
					wake_up_process(vpt->task);
				}
				break;
			}
		}

		if (i >= ARRAY_SIZE(p9_mux_poll_tasks))
			P9_DPRINTK(P9_DEBUG_ERROR,
					"warning: no free poll slots\n");
	}

	n = (p9_mux_num + 1) / p9_mux_poll_task_num +
	    ((p9_mux_num + 1) % p9_mux_poll_task_num ? 1 : 0);

	vptlast = NULL;
	for (i = 0; i < ARRAY_SIZE(p9_mux_poll_tasks); i++) {
		vpt = &p9_mux_poll_tasks[i];
		if (vpt->task != NULL) {
			vptlast = vpt;
			if (vpt->muxnum < n) {
				P9_DPRINTK(P9_DEBUG_MUX, "put in proc %d\n", i);
				list_add(&m->mux_list, &vpt->mux_list);
				vpt->muxnum++;
				m->poll_task = vpt;
				memset(&m->poll_waddr, 0,
							sizeof(m->poll_waddr));
				init_poll_funcptr(&m->pt, p9_pollwait);
				break;
			}
		}
	}

	if (i >= ARRAY_SIZE(p9_mux_poll_tasks)) {
		if (vptlast == NULL)
			return -ENOMEM;

		P9_DPRINTK(P9_DEBUG_MUX, "put in proc %d\n", i);
		list_add(&m->mux_list, &vptlast->mux_list);
		vptlast->muxnum++;
		m->poll_task = vptlast;
		memset(&m->poll_waddr, 0, sizeof(m->poll_waddr));
		init_poll_funcptr(&m->pt, p9_pollwait);
	}

	p9_mux_num++;
	mutex_unlock(&p9_mux_task_lock);

	return 0;
}

static void p9_mux_poll_stop(struct p9_conn *m)
{
	int i;
	struct p9_mux_poll_task *vpt;

	mutex_lock(&p9_mux_task_lock);
	vpt = m->poll_task;
	list_del(&m->mux_list);
	for (i = 0; i < ARRAY_SIZE(m->poll_waddr); i++) {
		if (m->poll_waddr[i] != NULL) {
			remove_wait_queue(m->poll_waddr[i], &m->poll_wait[i]);
			m->poll_waddr[i] = NULL;
		}
	}
	vpt->muxnum--;
	if (!vpt->muxnum) {
		P9_DPRINTK(P9_DEBUG_MUX, "destroy proc %p\n", vpt);
		kthread_stop(vpt->task);
		vpt->task = NULL;
		p9_mux_poll_task_num--;
	}
	p9_mux_num--;
	mutex_unlock(&p9_mux_task_lock);
}

/**
 * p9_conn_create - allocate and initialize the per-session mux data
 * Creates the polling task if this is the first session.
 *
 * @trans - transport structure
 * @msize - maximum message size
 * @extended - pointer to the extended flag
 */
struct p9_conn *p9_conn_create(struct p9_transport *trans, int msize,
				    unsigned char *extended)
{
	int i, n;
	struct p9_conn *m, *mtmp;

	P9_DPRINTK(P9_DEBUG_MUX, "transport %p msize %d\n", trans, msize);
	m = kmalloc(sizeof(struct p9_conn), GFP_KERNEL);
	if (!m)
		return ERR_PTR(-ENOMEM);

	spin_lock_init(&m->lock);
	INIT_LIST_HEAD(&m->mux_list);
	m->msize = msize;
	m->extended = extended;
	m->trans = trans;
	m->tagpool = p9_idpool_create();
	if (IS_ERR(m->tagpool)) {
		mtmp = ERR_PTR(-ENOMEM);
		kfree(m);
		return mtmp;
	}

	m->err = 0;
	init_waitqueue_head(&m->equeue);
	INIT_LIST_HEAD(&m->req_list);
	INIT_LIST_HEAD(&m->unsent_req_list);
	m->rcall = NULL;
	m->rpos = 0;
	m->rbuf = NULL;
	m->wpos = m->wsize = 0;
	m->wbuf = NULL;
	INIT_WORK(&m->rq, p9_read_work);
	INIT_WORK(&m->wq, p9_write_work);
	m->wsched = 0;
	memset(&m->poll_waddr, 0, sizeof(m->poll_waddr));
	m->poll_task = NULL;
	n = p9_mux_poll_start(m);
	if (n) {
		kfree(m);
		return ERR_PTR(n);
	}

	n = trans->poll(trans, &m->pt);
	if (n & POLLIN) {
		P9_DPRINTK(P9_DEBUG_MUX, "mux %p can read\n", m);
		set_bit(Rpending, &m->wsched);
	}

	if (n & POLLOUT) {
		P9_DPRINTK(P9_DEBUG_MUX, "mux %p can write\n", m);
		set_bit(Wpending, &m->wsched);
	}

	for (i = 0; i < ARRAY_SIZE(m->poll_waddr); i++) {
		if (IS_ERR(m->poll_waddr[i])) {
			p9_mux_poll_stop(m);
			mtmp = (void *)m->poll_waddr;	/* the error code */
			kfree(m);
			m = mtmp;
			break;
		}
	}

	return m;
}
EXPORT_SYMBOL(p9_conn_create);

/**
 * p9_mux_destroy - cancels all pending requests and frees mux resources
 */
void p9_conn_destroy(struct p9_conn *m)
{
	P9_DPRINTK(P9_DEBUG_MUX, "mux %p prev %p next %p\n", m,
		m->mux_list.prev, m->mux_list.next);
	p9_conn_cancel(m, -ECONNRESET);

	if (!list_empty(&m->req_list)) {
		/* wait until all processes waiting on this session exit */
		P9_DPRINTK(P9_DEBUG_MUX,
			"mux %p waiting for empty request queue\n", m);
		wait_event_timeout(m->equeue, (list_empty(&m->req_list)), 5000);
		P9_DPRINTK(P9_DEBUG_MUX, "mux %p request queue empty: %d\n", m,
			list_empty(&m->req_list));
	}

	p9_mux_poll_stop(m);
	m->trans = NULL;
	p9_idpool_destroy(m->tagpool);
	kfree(m);
}
EXPORT_SYMBOL(p9_conn_destroy);

/**
 * p9_pollwait - called by files poll operation to add v9fs-poll task
 * 	to files wait queue
 */
static void
p9_pollwait(struct file *filp, wait_queue_head_t *wait_address,
	      poll_table * p)
{
	int i;
	struct p9_conn *m;

	m = container_of(p, struct p9_conn, pt);
	for (i = 0; i < ARRAY_SIZE(m->poll_waddr); i++)
		if (m->poll_waddr[i] == NULL)
			break;

	if (i >= ARRAY_SIZE(m->poll_waddr)) {
		P9_DPRINTK(P9_DEBUG_ERROR, "not enough wait_address slots\n");
		return;
	}

	m->poll_waddr[i] = wait_address;

	if (!wait_address) {
		P9_DPRINTK(P9_DEBUG_ERROR, "no wait_address\n");
		m->poll_waddr[i] = ERR_PTR(-EIO);
		return;
	}

	init_waitqueue_entry(&m->poll_wait[i], m->poll_task->task);
	add_wait_queue(wait_address, &m->poll_wait[i]);
}

/**
 * p9_poll_mux - polls a mux and schedules read or write works if necessary
 */
static void p9_poll_mux(struct p9_conn *m)
{
	int n;

	if (m->err < 0)
		return;

	n = m->trans->poll(m->trans, NULL);
	if (n < 0 || n & (POLLERR | POLLHUP | POLLNVAL)) {
		P9_DPRINTK(P9_DEBUG_MUX, "error mux %p err %d\n", m, n);
		if (n >= 0)
			n = -ECONNRESET;
		p9_conn_cancel(m, n);
	}

	if (n & POLLIN) {
		set_bit(Rpending, &m->wsched);
		P9_DPRINTK(P9_DEBUG_MUX, "mux %p can read\n", m);
		if (!test_and_set_bit(Rworksched, &m->wsched)) {
			P9_DPRINTK(P9_DEBUG_MUX, "schedule read work %p\n", m);
			queue_work(p9_mux_wq, &m->rq);
		}
	}

	if (n & POLLOUT) {
		set_bit(Wpending, &m->wsched);
		P9_DPRINTK(P9_DEBUG_MUX, "mux %p can write\n", m);
		if ((m->wsize || !list_empty(&m->unsent_req_list))
		    && !test_and_set_bit(Wworksched, &m->wsched)) {
			P9_DPRINTK(P9_DEBUG_MUX, "schedule write work %p\n", m);
			queue_work(p9_mux_wq, &m->wq);
		}
	}
}

/**
 * p9_poll_proc - polls all v9fs transports for new events and queues
 * 	the appropriate work to the work queue
 */
static int p9_poll_proc(void *a)
{
	struct p9_conn *m, *mtmp;
	struct p9_mux_poll_task *vpt;

	vpt = a;
	P9_DPRINTK(P9_DEBUG_MUX, "start %p %p\n", current, vpt);
	while (!kthread_should_stop()) {
		set_current_state(TASK_INTERRUPTIBLE);

		list_for_each_entry_safe(m, mtmp, &vpt->mux_list, mux_list) {
			p9_poll_mux(m);
		}

		P9_DPRINTK(P9_DEBUG_MUX, "sleeping...\n");
		schedule_timeout(SCHED_TIMEOUT * HZ);
	}

	__set_current_state(TASK_RUNNING);
	P9_DPRINTK(P9_DEBUG_MUX, "finish\n");
	return 0;
}

/**
 * p9_write_work - called when a transport can send some data
 */
static void p9_write_work(struct work_struct *work)
{
	int n, err;
	struct p9_conn *m;
	struct p9_req *req;

	m = container_of(work, struct p9_conn, wq);

	if (m->err < 0) {
		clear_bit(Wworksched, &m->wsched);
		return;
	}

	if (!m->wsize) {
		if (list_empty(&m->unsent_req_list)) {
			clear_bit(Wworksched, &m->wsched);
			return;
		}

		spin_lock(&m->lock);
again:
		req = list_entry(m->unsent_req_list.next, struct p9_req,
			       req_list);
		list_move_tail(&req->req_list, &m->req_list);
		if (req->err == ERREQFLUSH)
			goto again;

		m->wbuf = req->tcall->sdata;
		m->wsize = req->tcall->size;
		m->wpos = 0;
		spin_unlock(&m->lock);
	}

	P9_DPRINTK(P9_DEBUG_MUX, "mux %p pos %d size %d\n", m, m->wpos,
								m->wsize);
	clear_bit(Wpending, &m->wsched);
	err = m->trans->write(m->trans, m->wbuf + m->wpos, m->wsize - m->wpos);
	P9_DPRINTK(P9_DEBUG_MUX, "mux %p sent %d bytes\n", m, err);
	if (err == -EAGAIN) {
		clear_bit(Wworksched, &m->wsched);
		return;
	}

	if (err < 0)
		goto error;
	else if (err == 0) {
		err = -EREMOTEIO;
		goto error;
	}

	m->wpos += err;
	if (m->wpos == m->wsize)
		m->wpos = m->wsize = 0;

	if (m->wsize == 0 && !list_empty(&m->unsent_req_list)) {
		if (test_and_clear_bit(Wpending, &m->wsched))
			n = POLLOUT;
		else
			n = m->trans->poll(m->trans, NULL);

		if (n & POLLOUT) {
			P9_DPRINTK(P9_DEBUG_MUX, "schedule write work %p\n", m);
			queue_work(p9_mux_wq, &m->wq);
		} else
			clear_bit(Wworksched, &m->wsched);
	} else
		clear_bit(Wworksched, &m->wsched);

	return;

error:
	p9_conn_cancel(m, err);
	clear_bit(Wworksched, &m->wsched);
}

static void process_request(struct p9_conn *m, struct p9_req *req)
{
	int ecode;
	struct p9_str *ename;

	if (!req->err && req->rcall->id == P9_RERROR) {
		ecode = req->rcall->params.rerror.errno;
		ename = &req->rcall->params.rerror.error;

		P9_DPRINTK(P9_DEBUG_MUX, "Rerror %.*s\n", ename->len,
								ename->str);

		if (*m->extended)
			req->err = -ecode;

		if (!req->err) {
			req->err = p9_errstr2errno(ename->str, ename->len);

			if (!req->err) {	/* string match failed */
				PRINT_FCALL_ERROR("unknown error", req->rcall);
			}

			if (!req->err)
				req->err = -ESERVERFAULT;
		}
	} else if (req->tcall && req->rcall->id != req->tcall->id + 1) {
		P9_DPRINTK(P9_DEBUG_ERROR,
				"fcall mismatch: expected %d, got %d\n",
				req->tcall->id + 1, req->rcall->id);
		if (!req->err)
			req->err = -EIO;
	}
}

/**
 * p9_read_work - called when there is some data to be read from a transport
 */
static void p9_read_work(struct work_struct *work)
{
	int n, err;
	struct p9_conn *m;
	struct p9_req *req, *rptr, *rreq;
	struct p9_fcall *rcall;
	char *rbuf;

	m = container_of(work, struct p9_conn, rq);

	if (m->err < 0)
		return;

	rcall = NULL;
	P9_DPRINTK(P9_DEBUG_MUX, "start mux %p pos %d\n", m, m->rpos);

	if (!m->rcall) {
		m->rcall =
		    kmalloc(sizeof(struct p9_fcall) + m->msize, GFP_KERNEL);
		if (!m->rcall) {
			err = -ENOMEM;
			goto error;
		}

		m->rbuf = (char *)m->rcall + sizeof(struct p9_fcall);
		m->rpos = 0;
	}

	clear_bit(Rpending, &m->wsched);
	err = m->trans->read(m->trans, m->rbuf + m->rpos, m->msize - m->rpos);
	P9_DPRINTK(P9_DEBUG_MUX, "mux %p got %d bytes\n", m, err);
	if (err == -EAGAIN) {
		clear_bit(Rworksched, &m->wsched);
		return;
	}

	if (err <= 0)
		goto error;

	m->rpos += err;
	while (m->rpos > 4) {
		n = le32_to_cpu(*(__le32 *) m->rbuf);
		if (n >= m->msize) {
			P9_DPRINTK(P9_DEBUG_ERROR,
				"requested packet size too big: %d\n", n);
			err = -EIO;
			goto error;
		}

		if (m->rpos < n)
			break;

		err =
		    p9_deserialize_fcall(m->rbuf, n, m->rcall, *m->extended);
		if (err < 0) {
			goto error;
		}

#ifdef CONFIG_NET_9P_DEBUG
		if ((p9_debug_level&P9_DEBUG_FCALL) == P9_DEBUG_FCALL) {
			char buf[150];

			p9_printfcall(buf, sizeof(buf), m->rcall,
				*m->extended);
			printk(KERN_NOTICE ">>> %p %s\n", m, buf);
		}
#endif

		rcall = m->rcall;
		rbuf = m->rbuf;
		if (m->rpos > n) {
			m->rcall = kmalloc(sizeof(struct p9_fcall) + m->msize,
					   GFP_KERNEL);
			if (!m->rcall) {
				err = -ENOMEM;
				goto error;
			}

			m->rbuf = (char *)m->rcall + sizeof(struct p9_fcall);
			memmove(m->rbuf, rbuf + n, m->rpos - n);
			m->rpos -= n;
		} else {
			m->rcall = NULL;
			m->rbuf = NULL;
			m->rpos = 0;
		}

		P9_DPRINTK(P9_DEBUG_MUX, "mux %p fcall id %d tag %d\n", m,
							rcall->id, rcall->tag);

		req = NULL;
		spin_lock(&m->lock);
		list_for_each_entry_safe(rreq, rptr, &m->req_list, req_list) {
			if (rreq->tag == rcall->tag) {
				req = rreq;
				if (req->flush != Flushing)
					list_del(&req->req_list);
				break;
			}
		}
		spin_unlock(&m->lock);

		if (req) {
			req->rcall = rcall;
			process_request(m, req);

			if (req->flush != Flushing) {
				if (req->cb)
					(*req->cb) (req, req->cba);
				else
					kfree(req->rcall);

				wake_up(&m->equeue);
			}
		} else {
			if (err >= 0 && rcall->id != P9_RFLUSH)
				P9_DPRINTK(P9_DEBUG_ERROR,
				  "unexpected response mux %p id %d tag %d\n",
				  m, rcall->id, rcall->tag);
			kfree(rcall);
		}
	}

	if (!list_empty(&m->req_list)) {
		if (test_and_clear_bit(Rpending, &m->wsched))
			n = POLLIN;
		else
			n = m->trans->poll(m->trans, NULL);

		if (n & POLLIN) {
			P9_DPRINTK(P9_DEBUG_MUX, "schedule read work %p\n", m);
			queue_work(p9_mux_wq, &m->rq);
		} else
			clear_bit(Rworksched, &m->wsched);
	} else
		clear_bit(Rworksched, &m->wsched);

	return;

error:
	p9_conn_cancel(m, err);
	clear_bit(Rworksched, &m->wsched);
}

/**
 * p9_send_request - send 9P request
 * The function can sleep until the request is scheduled for sending.
 * The function can be interrupted. Return from the function is not
 * a guarantee that the request is sent successfully. Can return errors
 * that can be retrieved by PTR_ERR macros.
 *
 * @m: mux data
 * @tc: request to be sent
 * @cb: callback function to call when response is received
 * @cba: parameter to pass to the callback function
 */
static struct p9_req *p9_send_request(struct p9_conn *m,
					  struct p9_fcall *tc,
					  p9_conn_req_callback cb, void *cba)
{
	int n;
	struct p9_req *req;

	P9_DPRINTK(P9_DEBUG_MUX, "mux %p task %p tcall %p id %d\n", m, current,
		tc, tc->id);
	if (m->err < 0)
		return ERR_PTR(m->err);

	req = kmalloc(sizeof(struct p9_req), GFP_KERNEL);
	if (!req)
		return ERR_PTR(-ENOMEM);

	if (tc->id == P9_TVERSION)
		n = P9_NOTAG;
	else
		n = p9_mux_get_tag(m);

	if (n < 0)
		return ERR_PTR(-ENOMEM);

	p9_set_tag(tc, n);

#ifdef CONFIG_NET_9P_DEBUG
	if ((p9_debug_level&P9_DEBUG_FCALL) == P9_DEBUG_FCALL) {
		char buf[150];

		p9_printfcall(buf, sizeof(buf), tc, *m->extended);
		printk(KERN_NOTICE "<<< %p %s\n", m, buf);
	}
#endif

	spin_lock_init(&req->lock);
	req->tag = n;
	req->tcall = tc;
	req->rcall = NULL;
	req->err = 0;
	req->cb = cb;
	req->cba = cba;
	req->flush = None;

	spin_lock(&m->lock);
	list_add_tail(&req->req_list, &m->unsent_req_list);
	spin_unlock(&m->lock);

	if (test_and_clear_bit(Wpending, &m->wsched))
		n = POLLOUT;
	else
		n = m->trans->poll(m->trans, NULL);

	if (n & POLLOUT && !test_and_set_bit(Wworksched, &m->wsched))
		queue_work(p9_mux_wq, &m->wq);

	return req;
}

static void p9_mux_free_request(struct p9_conn *m, struct p9_req *req)
{
	p9_mux_put_tag(m, req->tag);
	kfree(req);
}

static void p9_mux_flush_cb(struct p9_req *freq, void *a)
{
	p9_conn_req_callback cb;
	int tag;
	struct p9_conn *m;
	struct p9_req *req, *rreq, *rptr;

	m = a;
	P9_DPRINTK(P9_DEBUG_MUX, "mux %p tc %p rc %p err %d oldtag %d\n", m,
		freq->tcall, freq->rcall, freq->err,
		freq->tcall->params.tflush.oldtag);

	spin_lock(&m->lock);
	cb = NULL;
	tag = freq->tcall->params.tflush.oldtag;
	req = NULL;
	list_for_each_entry_safe(rreq, rptr, &m->req_list, req_list) {
		if (rreq->tag == tag) {
			req = rreq;
			list_del(&req->req_list);
			break;
		}
	}
	spin_unlock(&m->lock);

	if (req) {
		spin_lock(&req->lock);
		req->flush = Flushed;
		spin_unlock(&req->lock);

		if (req->cb)
			(*req->cb) (req, req->cba);
		else
			kfree(req->rcall);

		wake_up(&m->equeue);
	}

	kfree(freq->tcall);
	kfree(freq->rcall);
	p9_mux_free_request(m, freq);
}

static int
p9_mux_flush_request(struct p9_conn *m, struct p9_req *req)
{
	struct p9_fcall *fc;
	struct p9_req *rreq, *rptr;

	P9_DPRINTK(P9_DEBUG_MUX, "mux %p req %p tag %d\n", m, req, req->tag);

	/* if a response was received for a request, do nothing */
	spin_lock(&req->lock);
	if (req->rcall || req->err) {
		spin_unlock(&req->lock);
		P9_DPRINTK(P9_DEBUG_MUX,
			"mux %p req %p response already received\n", m, req);
		return 0;
	}

	req->flush = Flushing;
	spin_unlock(&req->lock);

	spin_lock(&m->lock);
	/* if the request is not sent yet, just remove it from the list */
	list_for_each_entry_safe(rreq, rptr, &m->unsent_req_list, req_list) {
		if (rreq->tag == req->tag) {
			P9_DPRINTK(P9_DEBUG_MUX,
			   "mux %p req %p request is not sent yet\n", m, req);
			list_del(&rreq->req_list);
			req->flush = Flushed;
			spin_unlock(&m->lock);
			if (req->cb)
				(*req->cb) (req, req->cba);
			return 0;
		}
	}
	spin_unlock(&m->lock);

	clear_thread_flag(TIF_SIGPENDING);
	fc = p9_create_tflush(req->tag);
	p9_send_request(m, fc, p9_mux_flush_cb, m);
	return 1;
}

static void
p9_conn_rpc_cb(struct p9_req *req, void *a)
{
	struct p9_mux_rpc *r;

	P9_DPRINTK(P9_DEBUG_MUX, "req %p r %p\n", req, a);
	r = a;
	r->rcall = req->rcall;
	r->err = req->err;

	if (req->flush != None && !req->err)
		r->err = -ERESTARTSYS;

	wake_up(&r->wqueue);
}

/**
 * p9_mux_rpc - sends 9P request and waits until a response is available.
 *	The function can be interrupted.
 * @m: mux data
 * @tc: request to be sent
 * @rc: pointer where a pointer to the response is stored
 */
int
p9_conn_rpc(struct p9_conn *m, struct p9_fcall *tc,
	     struct p9_fcall **rc)
{
	int err, sigpending;
	unsigned long flags;
	struct p9_req *req;
	struct p9_mux_rpc r;

	r.err = 0;
	r.tcall = tc;
	r.rcall = NULL;
	r.m = m;
	init_waitqueue_head(&r.wqueue);

	if (rc)
		*rc = NULL;

	sigpending = 0;
	if (signal_pending(current)) {
		sigpending = 1;
		clear_thread_flag(TIF_SIGPENDING);
	}

	req = p9_send_request(m, tc, p9_conn_rpc_cb, &r);
	if (IS_ERR(req)) {
		err = PTR_ERR(req);
		P9_DPRINTK(P9_DEBUG_MUX, "error %d\n", err);
		return err;
	}

	err = wait_event_interruptible(r.wqueue, r.rcall != NULL || r.err < 0);
	if (r.err < 0)
		err = r.err;

	if (err == -ERESTARTSYS && m->trans->status == Connected
							&& m->err == 0) {
		if (p9_mux_flush_request(m, req)) {
			/* wait until we get response of the flush message */
			do {
				clear_thread_flag(TIF_SIGPENDING);
				err = wait_event_interruptible(r.wqueue,
					r.rcall || r.err);
			} while (!r.rcall && !r.err && err == -ERESTARTSYS &&
				m->trans->status == Connected && !m->err);

			err = -ERESTARTSYS;
		}
		sigpending = 1;
	}

	if (sigpending) {
		spin_lock_irqsave(&current->sighand->siglock, flags);
		recalc_sigpending();
		spin_unlock_irqrestore(&current->sighand->siglock, flags);
	}

	if (rc)
		*rc = r.rcall;
	else
		kfree(r.rcall);

	p9_mux_free_request(m, req);
	if (err > 0)
		err = -EIO;

	return err;
}
EXPORT_SYMBOL(p9_conn_rpc);

#ifdef P9_NONBLOCK
/**
 * p9_conn_rpcnb - sends 9P request without waiting for response.
 * @m: mux data
 * @tc: request to be sent
 * @cb: callback function to be called when response arrives
 * @cba: value to pass to the callback function
 */
int p9_conn_rpcnb(struct p9_conn *m, struct p9_fcall *tc,
		   p9_conn_req_callback cb, void *a)
{
	int err;
	struct p9_req *req;

	req = p9_send_request(m, tc, cb, a);
	if (IS_ERR(req)) {
		err = PTR_ERR(req);
		P9_DPRINTK(P9_DEBUG_MUX, "error %d\n", err);
		return PTR_ERR(req);
	}

	P9_DPRINTK(P9_DEBUG_MUX, "mux %p tc %p tag %d\n", m, tc, req->tag);
	return 0;
}
EXPORT_SYMBOL(p9_conn_rpcnb);
#endif /* P9_NONBLOCK */

/**
 * p9_conn_cancel - cancel all pending requests with error
 * @m: mux data
 * @err: error code
 */
void p9_conn_cancel(struct p9_conn *m, int err)
{
	struct p9_req *req, *rtmp;
	LIST_HEAD(cancel_list);

	P9_DPRINTK(P9_DEBUG_ERROR, "mux %p err %d\n", m, err);
	m->err = err;
	spin_lock(&m->lock);
	list_for_each_entry_safe(req, rtmp, &m->req_list, req_list) {
		list_move(&req->req_list, &cancel_list);
	}
	list_for_each_entry_safe(req, rtmp, &m->unsent_req_list, req_list) {
		list_move(&req->req_list, &cancel_list);
	}
	spin_unlock(&m->lock);

	list_for_each_entry_safe(req, rtmp, &cancel_list, req_list) {
		list_del(&req->req_list);
		if (!req->err)
			req->err = err;

		if (req->cb)
			(*req->cb) (req, req->cba);
		else
			kfree(req->rcall);
	}

	wake_up(&m->equeue);
}
EXPORT_SYMBOL(p9_conn_cancel);

static u16 p9_mux_get_tag(struct p9_conn *m)
{
	int tag;

	tag = p9_idpool_get(m->tagpool);
	if (tag < 0)
		return P9_NOTAG;
	else
		return (u16) tag;
}

static void p9_mux_put_tag(struct p9_conn *m, u16 tag)
{
	if (tag != P9_NOTAG && p9_idpool_check(tag, m->tagpool))
		p9_idpool_put(tag, m->tagpool);
}
