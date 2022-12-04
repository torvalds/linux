// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/fs/9p/trans_fd.c
 *
 * Fd transport layer.  Includes deprecated socket layer.
 *
 *  Copyright (C) 2006 by Russ Cox <rsc@swtch.com>
 *  Copyright (C) 2004-2005 by Latchesar Ionkov <lucho@ionkov.net>
 *  Copyright (C) 2004-2008 by Eric Van Hensbergen <ericvh@gmail.com>
 *  Copyright (C) 1997-2002 by Ron Minnich <rminnich@sarnoff.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/in.h>
#include <linux/module.h>
#include <linux/net.h>
#include <linux/ipv6.h>
#include <linux/kthread.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/un.h>
#include <linux/uaccess.h>
#include <linux/inet.h>
#include <linux/idr.h>
#include <linux/file.h>
#include <linux/parser.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <net/9p/9p.h>
#include <net/9p/client.h>
#include <net/9p/transport.h>

#include <linux/syscalls.h> /* killme */

#define P9_PORT 564
#define MAX_SOCK_BUF (64*1024)
#define MAXPOLLWADDR	2

static struct p9_trans_module p9_tcp_trans;
static struct p9_trans_module p9_fd_trans;

/**
 * struct p9_fd_opts - per-transport options
 * @rfd: file descriptor for reading (trans=fd)
 * @wfd: file descriptor for writing (trans=fd)
 * @port: port to connect to (trans=tcp)
 *
 */

struct p9_fd_opts {
	int rfd;
	int wfd;
	u16 port;
	bool privport;
};

/*
  * Option Parsing (code inspired by NFS code)
  *  - a little lazy - parse all fd-transport options
  */

enum {
	/* Options that take integer arguments */
	Opt_port, Opt_rfdno, Opt_wfdno, Opt_err,
	/* Options that take no arguments */
	Opt_privport,
};

static const match_table_t tokens = {
	{Opt_port, "port=%u"},
	{Opt_rfdno, "rfdno=%u"},
	{Opt_wfdno, "wfdno=%u"},
	{Opt_privport, "privport"},
	{Opt_err, NULL},
};

enum {
	Rworksched = 1,		/* read work scheduled or running */
	Rpending = 2,		/* can read */
	Wworksched = 4,		/* write work scheduled or running */
	Wpending = 8,		/* can write */
};

struct p9_poll_wait {
	struct p9_conn *conn;
	wait_queue_entry_t wait;
	wait_queue_head_t *wait_addr;
};

/**
 * struct p9_conn - fd mux connection state information
 * @mux_list: list link for mux to manage multiple connections (?)
 * @client: reference to client instance for this connection
 * @err: error state
 * @req_list: accounting for requests which have been sent
 * @unsent_req_list: accounting for requests that haven't been sent
 * @req: current request being processed (if any)
 * @tmp_buf: temporary buffer to read in header
 * @rc: temporary fcall for reading current frame
 * @wpos: write position for current frame
 * @wsize: amount of data to write for current frame
 * @wbuf: current write buffer
 * @poll_pending_link: pending links to be polled per conn
 * @poll_wait: array of wait_q's for various worker threads
 * @pt: poll state
 * @rq: current read work
 * @wq: current write work
 * @wsched: ????
 *
 */

struct p9_conn {
	struct list_head mux_list;
	struct p9_client *client;
	int err;
	struct list_head req_list;
	struct list_head unsent_req_list;
	struct p9_req_t *rreq;
	struct p9_req_t *wreq;
	char tmp_buf[7];
	struct p9_fcall rc;
	int wpos;
	int wsize;
	char *wbuf;
	struct list_head poll_pending_link;
	struct p9_poll_wait poll_wait[MAXPOLLWADDR];
	poll_table pt;
	struct work_struct rq;
	struct work_struct wq;
	unsigned long wsched;
};

/**
 * struct p9_trans_fd - transport state
 * @rd: reference to file to read from
 * @wr: reference of file to write to
 * @conn: connection state reference
 *
 */

struct p9_trans_fd {
	struct file *rd;
	struct file *wr;
	struct p9_conn conn;
};

static void p9_poll_workfn(struct work_struct *work);

static DEFINE_SPINLOCK(p9_poll_lock);
static LIST_HEAD(p9_poll_pending_list);
static DECLARE_WORK(p9_poll_work, p9_poll_workfn);

static unsigned int p9_ipport_resv_min = P9_DEF_MIN_RESVPORT;
static unsigned int p9_ipport_resv_max = P9_DEF_MAX_RESVPORT;

static void p9_mux_poll_stop(struct p9_conn *m)
{
	unsigned long flags;
	int i;

	for (i = 0; i < ARRAY_SIZE(m->poll_wait); i++) {
		struct p9_poll_wait *pwait = &m->poll_wait[i];

		if (pwait->wait_addr) {
			remove_wait_queue(pwait->wait_addr, &pwait->wait);
			pwait->wait_addr = NULL;
		}
	}

	spin_lock_irqsave(&p9_poll_lock, flags);
	list_del_init(&m->poll_pending_link);
	spin_unlock_irqrestore(&p9_poll_lock, flags);

	flush_work(&p9_poll_work);
}

/**
 * p9_conn_cancel - cancel all pending requests with error
 * @m: mux data
 * @err: error code
 *
 */

static void p9_conn_cancel(struct p9_conn *m, int err)
{
	struct p9_req_t *req, *rtmp;
	LIST_HEAD(cancel_list);

	p9_debug(P9_DEBUG_ERROR, "mux %p err %d\n", m, err);

	spin_lock(&m->client->lock);

	if (m->err) {
		spin_unlock(&m->client->lock);
		return;
	}

	m->err = err;

	list_for_each_entry_safe(req, rtmp, &m->req_list, req_list) {
		list_move(&req->req_list, &cancel_list);
		req->status = REQ_STATUS_ERROR;
	}
	list_for_each_entry_safe(req, rtmp, &m->unsent_req_list, req_list) {
		list_move(&req->req_list, &cancel_list);
		req->status = REQ_STATUS_ERROR;
	}

	spin_unlock(&m->client->lock);

	list_for_each_entry_safe(req, rtmp, &cancel_list, req_list) {
		p9_debug(P9_DEBUG_ERROR, "call back req %p\n", req);
		list_del(&req->req_list);
		if (!req->t_err)
			req->t_err = err;
		p9_client_cb(m->client, req, REQ_STATUS_ERROR);
	}
}

static __poll_t
p9_fd_poll(struct p9_client *client, struct poll_table_struct *pt, int *err)
{
	__poll_t ret;
	struct p9_trans_fd *ts = NULL;

	if (client && client->status == Connected)
		ts = client->trans;

	if (!ts) {
		if (err)
			*err = -EREMOTEIO;
		return EPOLLERR;
	}

	ret = vfs_poll(ts->rd, pt);
	if (ts->rd != ts->wr)
		ret = (ret & ~EPOLLOUT) | (vfs_poll(ts->wr, pt) & ~EPOLLIN);
	return ret;
}

/**
 * p9_fd_read- read from a fd
 * @client: client instance
 * @v: buffer to receive data into
 * @len: size of receive buffer
 *
 */

static int p9_fd_read(struct p9_client *client, void *v, int len)
{
	int ret;
	struct p9_trans_fd *ts = NULL;
	loff_t pos;

	if (client && client->status != Disconnected)
		ts = client->trans;

	if (!ts)
		return -EREMOTEIO;

	if (!(ts->rd->f_flags & O_NONBLOCK))
		p9_debug(P9_DEBUG_ERROR, "blocking read ...\n");

	pos = ts->rd->f_pos;
	ret = kernel_read(ts->rd, v, len, &pos);
	if (ret <= 0 && ret != -ERESTARTSYS && ret != -EAGAIN)
		client->status = Disconnected;
	return ret;
}

/**
 * p9_read_work - called when there is some data to be read from a transport
 * @work: container of work to be done
 *
 */

static void p9_read_work(struct work_struct *work)
{
	__poll_t n;
	int err;
	struct p9_conn *m;

	m = container_of(work, struct p9_conn, rq);

	if (m->err < 0)
		return;

	p9_debug(P9_DEBUG_TRANS, "start mux %p pos %zd\n", m, m->rc.offset);

	if (!m->rc.sdata) {
		m->rc.sdata = m->tmp_buf;
		m->rc.offset = 0;
		m->rc.capacity = 7; /* start by reading header */
	}

	clear_bit(Rpending, &m->wsched);
	p9_debug(P9_DEBUG_TRANS, "read mux %p pos %zd size: %zd = %zd\n",
		 m, m->rc.offset, m->rc.capacity,
		 m->rc.capacity - m->rc.offset);
	err = p9_fd_read(m->client, m->rc.sdata + m->rc.offset,
			 m->rc.capacity - m->rc.offset);
	p9_debug(P9_DEBUG_TRANS, "mux %p got %d bytes\n", m, err);
	if (err == -EAGAIN)
		goto end_clear;

	if (err <= 0)
		goto error;

	m->rc.offset += err;

	/* header read in */
	if ((!m->rreq) && (m->rc.offset == m->rc.capacity)) {
		p9_debug(P9_DEBUG_TRANS, "got new header\n");

		/* Header size */
		m->rc.size = 7;
		err = p9_parse_header(&m->rc, &m->rc.size, NULL, NULL, 0);
		if (err) {
			p9_debug(P9_DEBUG_ERROR,
				 "error parsing header: %d\n", err);
			goto error;
		}

		if (m->rc.size >= m->client->msize) {
			p9_debug(P9_DEBUG_ERROR,
				 "requested packet size too big: %d\n",
				 m->rc.size);
			err = -EIO;
			goto error;
		}

		p9_debug(P9_DEBUG_TRANS,
			 "mux %p pkt: size: %d bytes tag: %d\n",
			 m, m->rc.size, m->rc.tag);

		m->rreq = p9_tag_lookup(m->client, m->rc.tag);
		if (!m->rreq || (m->rreq->status != REQ_STATUS_SENT)) {
			p9_debug(P9_DEBUG_ERROR, "Unexpected packet tag %d\n",
				 m->rc.tag);
			err = -EIO;
			goto error;
		}

		if (!m->rreq->rc.sdata) {
			p9_debug(P9_DEBUG_ERROR,
				 "No recv fcall for tag %d (req %p), disconnecting!\n",
				 m->rc.tag, m->rreq);
			m->rreq = NULL;
			err = -EIO;
			goto error;
		}
		m->rc.sdata = m->rreq->rc.sdata;
		memcpy(m->rc.sdata, m->tmp_buf, m->rc.capacity);
		m->rc.capacity = m->rc.size;
	}

	/* packet is read in
	 * not an else because some packets (like clunk) have no payload
	 */
	if ((m->rreq) && (m->rc.offset == m->rc.capacity)) {
		p9_debug(P9_DEBUG_TRANS, "got new packet\n");
		m->rreq->rc.size = m->rc.offset;
		spin_lock(&m->client->lock);
		if (m->rreq->status == REQ_STATUS_SENT) {
			list_del(&m->rreq->req_list);
			p9_client_cb(m->client, m->rreq, REQ_STATUS_RCVD);
		} else if (m->rreq->status == REQ_STATUS_FLSHD) {
			/* Ignore replies associated with a cancelled request. */
			p9_debug(P9_DEBUG_TRANS,
				 "Ignore replies associated with a cancelled request\n");
		} else {
			spin_unlock(&m->client->lock);
			p9_debug(P9_DEBUG_ERROR,
				 "Request tag %d errored out while we were reading the reply\n",
				 m->rc.tag);
			err = -EIO;
			goto error;
		}
		spin_unlock(&m->client->lock);
		m->rc.sdata = NULL;
		m->rc.offset = 0;
		m->rc.capacity = 0;
		p9_req_put(m->rreq);
		m->rreq = NULL;
	}

end_clear:
	clear_bit(Rworksched, &m->wsched);

	if (!list_empty(&m->req_list)) {
		if (test_and_clear_bit(Rpending, &m->wsched))
			n = EPOLLIN;
		else
			n = p9_fd_poll(m->client, NULL, NULL);

		if ((n & EPOLLIN) && !test_and_set_bit(Rworksched, &m->wsched)) {
			p9_debug(P9_DEBUG_TRANS, "sched read work %p\n", m);
			schedule_work(&m->rq);
		}
	}

	return;
error:
	p9_conn_cancel(m, err);
	clear_bit(Rworksched, &m->wsched);
}

/**
 * p9_fd_write - write to a socket
 * @client: client instance
 * @v: buffer to send data from
 * @len: size of send buffer
 *
 */

static int p9_fd_write(struct p9_client *client, void *v, int len)
{
	ssize_t ret;
	struct p9_trans_fd *ts = NULL;

	if (client && client->status != Disconnected)
		ts = client->trans;

	if (!ts)
		return -EREMOTEIO;

	if (!(ts->wr->f_flags & O_NONBLOCK))
		p9_debug(P9_DEBUG_ERROR, "blocking write ...\n");

	ret = kernel_write(ts->wr, v, len, &ts->wr->f_pos);
	if (ret <= 0 && ret != -ERESTARTSYS && ret != -EAGAIN)
		client->status = Disconnected;
	return ret;
}

/**
 * p9_write_work - called when a transport can send some data
 * @work: container for work to be done
 *
 */

static void p9_write_work(struct work_struct *work)
{
	__poll_t n;
	int err;
	struct p9_conn *m;
	struct p9_req_t *req;

	m = container_of(work, struct p9_conn, wq);

	if (m->err < 0) {
		clear_bit(Wworksched, &m->wsched);
		return;
	}

	if (!m->wsize) {
		spin_lock(&m->client->lock);
		if (list_empty(&m->unsent_req_list)) {
			clear_bit(Wworksched, &m->wsched);
			spin_unlock(&m->client->lock);
			return;
		}

		req = list_entry(m->unsent_req_list.next, struct p9_req_t,
			       req_list);
		req->status = REQ_STATUS_SENT;
		p9_debug(P9_DEBUG_TRANS, "move req %p\n", req);
		list_move_tail(&req->req_list, &m->req_list);

		m->wbuf = req->tc.sdata;
		m->wsize = req->tc.size;
		m->wpos = 0;
		p9_req_get(req);
		m->wreq = req;
		spin_unlock(&m->client->lock);
	}

	p9_debug(P9_DEBUG_TRANS, "mux %p pos %d size %d\n",
		 m, m->wpos, m->wsize);
	clear_bit(Wpending, &m->wsched);
	err = p9_fd_write(m->client, m->wbuf + m->wpos, m->wsize - m->wpos);
	p9_debug(P9_DEBUG_TRANS, "mux %p sent %d bytes\n", m, err);
	if (err == -EAGAIN)
		goto end_clear;


	if (err < 0)
		goto error;
	else if (err == 0) {
		err = -EREMOTEIO;
		goto error;
	}

	m->wpos += err;
	if (m->wpos == m->wsize) {
		m->wpos = m->wsize = 0;
		p9_req_put(m->wreq);
		m->wreq = NULL;
	}

end_clear:
	clear_bit(Wworksched, &m->wsched);

	if (m->wsize || !list_empty(&m->unsent_req_list)) {
		if (test_and_clear_bit(Wpending, &m->wsched))
			n = EPOLLOUT;
		else
			n = p9_fd_poll(m->client, NULL, NULL);

		if ((n & EPOLLOUT) &&
		   !test_and_set_bit(Wworksched, &m->wsched)) {
			p9_debug(P9_DEBUG_TRANS, "sched write work %p\n", m);
			schedule_work(&m->wq);
		}
	}

	return;

error:
	p9_conn_cancel(m, err);
	clear_bit(Wworksched, &m->wsched);
}

static int p9_pollwake(wait_queue_entry_t *wait, unsigned int mode, int sync, void *key)
{
	struct p9_poll_wait *pwait =
		container_of(wait, struct p9_poll_wait, wait);
	struct p9_conn *m = pwait->conn;
	unsigned long flags;

	spin_lock_irqsave(&p9_poll_lock, flags);
	if (list_empty(&m->poll_pending_link))
		list_add_tail(&m->poll_pending_link, &p9_poll_pending_list);
	spin_unlock_irqrestore(&p9_poll_lock, flags);

	schedule_work(&p9_poll_work);
	return 1;
}

/**
 * p9_pollwait - add poll task to the wait queue
 * @filp: file pointer being polled
 * @wait_address: wait_q to block on
 * @p: poll state
 *
 * called by files poll operation to add v9fs-poll task to files wait queue
 */

static void
p9_pollwait(struct file *filp, wait_queue_head_t *wait_address, poll_table *p)
{
	struct p9_conn *m = container_of(p, struct p9_conn, pt);
	struct p9_poll_wait *pwait = NULL;
	int i;

	for (i = 0; i < ARRAY_SIZE(m->poll_wait); i++) {
		if (m->poll_wait[i].wait_addr == NULL) {
			pwait = &m->poll_wait[i];
			break;
		}
	}

	if (!pwait) {
		p9_debug(P9_DEBUG_ERROR, "not enough wait_address slots\n");
		return;
	}

	pwait->conn = m;
	pwait->wait_addr = wait_address;
	init_waitqueue_func_entry(&pwait->wait, p9_pollwake);
	add_wait_queue(wait_address, &pwait->wait);
}

/**
 * p9_conn_create - initialize the per-session mux data
 * @client: client instance
 *
 * Note: Creates the polling task if this is the first session.
 */

static void p9_conn_create(struct p9_client *client)
{
	__poll_t n;
	struct p9_trans_fd *ts = client->trans;
	struct p9_conn *m = &ts->conn;

	p9_debug(P9_DEBUG_TRANS, "client %p msize %d\n", client, client->msize);

	INIT_LIST_HEAD(&m->mux_list);
	m->client = client;

	INIT_LIST_HEAD(&m->req_list);
	INIT_LIST_HEAD(&m->unsent_req_list);
	INIT_WORK(&m->rq, p9_read_work);
	INIT_WORK(&m->wq, p9_write_work);
	INIT_LIST_HEAD(&m->poll_pending_link);
	init_poll_funcptr(&m->pt, p9_pollwait);

	n = p9_fd_poll(client, &m->pt, NULL);
	if (n & EPOLLIN) {
		p9_debug(P9_DEBUG_TRANS, "mux %p can read\n", m);
		set_bit(Rpending, &m->wsched);
	}

	if (n & EPOLLOUT) {
		p9_debug(P9_DEBUG_TRANS, "mux %p can write\n", m);
		set_bit(Wpending, &m->wsched);
	}
}

/**
 * p9_poll_mux - polls a mux and schedules read or write works if necessary
 * @m: connection to poll
 *
 */

static void p9_poll_mux(struct p9_conn *m)
{
	__poll_t n;
	int err = -ECONNRESET;

	if (m->err < 0)
		return;

	n = p9_fd_poll(m->client, NULL, &err);
	if (n & (EPOLLERR | EPOLLHUP | EPOLLNVAL)) {
		p9_debug(P9_DEBUG_TRANS, "error mux %p err %d\n", m, n);
		p9_conn_cancel(m, err);
	}

	if (n & EPOLLIN) {
		set_bit(Rpending, &m->wsched);
		p9_debug(P9_DEBUG_TRANS, "mux %p can read\n", m);
		if (!test_and_set_bit(Rworksched, &m->wsched)) {
			p9_debug(P9_DEBUG_TRANS, "sched read work %p\n", m);
			schedule_work(&m->rq);
		}
	}

	if (n & EPOLLOUT) {
		set_bit(Wpending, &m->wsched);
		p9_debug(P9_DEBUG_TRANS, "mux %p can write\n", m);
		if ((m->wsize || !list_empty(&m->unsent_req_list)) &&
		    !test_and_set_bit(Wworksched, &m->wsched)) {
			p9_debug(P9_DEBUG_TRANS, "sched write work %p\n", m);
			schedule_work(&m->wq);
		}
	}
}

/**
 * p9_fd_request - send 9P request
 * The function can sleep until the request is scheduled for sending.
 * The function can be interrupted. Return from the function is not
 * a guarantee that the request is sent successfully.
 *
 * @client: client instance
 * @req: request to be sent
 *
 */

static int p9_fd_request(struct p9_client *client, struct p9_req_t *req)
{
	__poll_t n;
	struct p9_trans_fd *ts = client->trans;
	struct p9_conn *m = &ts->conn;

	p9_debug(P9_DEBUG_TRANS, "mux %p task %p tcall %p id %d\n",
		 m, current, &req->tc, req->tc.id);
	if (m->err < 0)
		return m->err;

	spin_lock(&client->lock);
	req->status = REQ_STATUS_UNSENT;
	list_add_tail(&req->req_list, &m->unsent_req_list);
	spin_unlock(&client->lock);

	if (test_and_clear_bit(Wpending, &m->wsched))
		n = EPOLLOUT;
	else
		n = p9_fd_poll(m->client, NULL, NULL);

	if (n & EPOLLOUT && !test_and_set_bit(Wworksched, &m->wsched))
		schedule_work(&m->wq);

	return 0;
}

static int p9_fd_cancel(struct p9_client *client, struct p9_req_t *req)
{
	int ret = 1;

	p9_debug(P9_DEBUG_TRANS, "client %p req %p\n", client, req);

	spin_lock(&client->lock);

	if (req->status == REQ_STATUS_UNSENT) {
		list_del(&req->req_list);
		req->status = REQ_STATUS_FLSHD;
		p9_req_put(req);
		ret = 0;
	}
	spin_unlock(&client->lock);

	return ret;
}

static int p9_fd_cancelled(struct p9_client *client, struct p9_req_t *req)
{
	p9_debug(P9_DEBUG_TRANS, "client %p req %p\n", client, req);

	spin_lock(&client->lock);
	/* Ignore cancelled request if message has been received
	 * before lock.
	 */
	if (req->status == REQ_STATUS_RCVD) {
		spin_unlock(&client->lock);
		return 0;
	}

	/* we haven't received a response for oldreq,
	 * remove it from the list.
	 */
	list_del(&req->req_list);
	req->status = REQ_STATUS_FLSHD;
	spin_unlock(&client->lock);
	p9_req_put(req);

	return 0;
}

static int p9_fd_show_options(struct seq_file *m, struct p9_client *clnt)
{
	if (clnt->trans_mod == &p9_tcp_trans) {
		if (clnt->trans_opts.tcp.port != P9_PORT)
			seq_printf(m, ",port=%u", clnt->trans_opts.tcp.port);
	} else if (clnt->trans_mod == &p9_fd_trans) {
		if (clnt->trans_opts.fd.rfd != ~0)
			seq_printf(m, ",rfd=%u", clnt->trans_opts.fd.rfd);
		if (clnt->trans_opts.fd.wfd != ~0)
			seq_printf(m, ",wfd=%u", clnt->trans_opts.fd.wfd);
	}
	return 0;
}

/**
 * parse_opts - parse mount options into p9_fd_opts structure
 * @params: options string passed from mount
 * @opts: fd transport-specific structure to parse options into
 *
 * Returns 0 upon success, -ERRNO upon failure
 */

static int parse_opts(char *params, struct p9_fd_opts *opts)
{
	char *p;
	substring_t args[MAX_OPT_ARGS];
	int option;
	char *options, *tmp_options;

	opts->port = P9_PORT;
	opts->rfd = ~0;
	opts->wfd = ~0;
	opts->privport = false;

	if (!params)
		return 0;

	tmp_options = kstrdup(params, GFP_KERNEL);
	if (!tmp_options) {
		p9_debug(P9_DEBUG_ERROR,
			 "failed to allocate copy of option string\n");
		return -ENOMEM;
	}
	options = tmp_options;

	while ((p = strsep(&options, ",")) != NULL) {
		int token;
		int r;
		if (!*p)
			continue;
		token = match_token(p, tokens, args);
		if ((token != Opt_err) && (token != Opt_privport)) {
			r = match_int(&args[0], &option);
			if (r < 0) {
				p9_debug(P9_DEBUG_ERROR,
					 "integer field, but no integer?\n");
				continue;
			}
		}
		switch (token) {
		case Opt_port:
			opts->port = option;
			break;
		case Opt_rfdno:
			opts->rfd = option;
			break;
		case Opt_wfdno:
			opts->wfd = option;
			break;
		case Opt_privport:
			opts->privport = true;
			break;
		default:
			continue;
		}
	}

	kfree(tmp_options);
	return 0;
}

static int p9_fd_open(struct p9_client *client, int rfd, int wfd)
{
	struct p9_trans_fd *ts = kzalloc(sizeof(struct p9_trans_fd),
					   GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	ts->rd = fget(rfd);
	if (!ts->rd)
		goto out_free_ts;
	if (!(ts->rd->f_mode & FMODE_READ))
		goto out_put_rd;
	/* prevent workers from hanging on IO when fd is a pipe */
	ts->rd->f_flags |= O_NONBLOCK;
	ts->wr = fget(wfd);
	if (!ts->wr)
		goto out_put_rd;
	if (!(ts->wr->f_mode & FMODE_WRITE))
		goto out_put_wr;
	ts->wr->f_flags |= O_NONBLOCK;

	client->trans = ts;
	client->status = Connected;

	return 0;

out_put_wr:
	fput(ts->wr);
out_put_rd:
	fput(ts->rd);
out_free_ts:
	kfree(ts);
	return -EIO;
}

static int p9_socket_open(struct p9_client *client, struct socket *csocket)
{
	struct p9_trans_fd *p;
	struct file *file;

	p = kzalloc(sizeof(struct p9_trans_fd), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	csocket->sk->sk_allocation = GFP_NOIO;
	file = sock_alloc_file(csocket, 0, NULL);
	if (IS_ERR(file)) {
		pr_err("%s (%d): failed to map fd\n",
		       __func__, task_pid_nr(current));
		kfree(p);
		return PTR_ERR(file);
	}

	get_file(file);
	p->wr = p->rd = file;
	client->trans = p;
	client->status = Connected;

	p->rd->f_flags |= O_NONBLOCK;

	p9_conn_create(client);
	return 0;
}

/**
 * p9_mux_destroy - cancels all pending requests of mux
 * @m: mux to destroy
 *
 */

static void p9_conn_destroy(struct p9_conn *m)
{
	p9_debug(P9_DEBUG_TRANS, "mux %p prev %p next %p\n",
		 m, m->mux_list.prev, m->mux_list.next);

	p9_mux_poll_stop(m);
	cancel_work_sync(&m->rq);
	if (m->rreq) {
		p9_req_put(m->rreq);
		m->rreq = NULL;
	}
	cancel_work_sync(&m->wq);
	if (m->wreq) {
		p9_req_put(m->wreq);
		m->wreq = NULL;
	}

	p9_conn_cancel(m, -ECONNRESET);

	m->client = NULL;
}

/**
 * p9_fd_close - shutdown file descriptor transport
 * @client: client instance
 *
 */

static void p9_fd_close(struct p9_client *client)
{
	struct p9_trans_fd *ts;

	if (!client)
		return;

	ts = client->trans;
	if (!ts)
		return;

	client->status = Disconnected;

	p9_conn_destroy(&ts->conn);

	if (ts->rd)
		fput(ts->rd);
	if (ts->wr)
		fput(ts->wr);

	kfree(ts);
}

/*
 * stolen from NFS - maybe should be made a generic function?
 */
static inline int valid_ipaddr4(const char *buf)
{
	int rc, count, in[4];

	rc = sscanf(buf, "%d.%d.%d.%d", &in[0], &in[1], &in[2], &in[3]);
	if (rc != 4)
		return -EINVAL;
	for (count = 0; count < 4; count++) {
		if (in[count] > 255)
			return -EINVAL;
	}
	return 0;
}

static int p9_bind_privport(struct socket *sock)
{
	struct sockaddr_in cl;
	int port, err = -EINVAL;

	memset(&cl, 0, sizeof(cl));
	cl.sin_family = AF_INET;
	cl.sin_addr.s_addr = htonl(INADDR_ANY);
	for (port = p9_ipport_resv_max; port >= p9_ipport_resv_min; port--) {
		cl.sin_port = htons((ushort)port);
		err = kernel_bind(sock, (struct sockaddr *)&cl, sizeof(cl));
		if (err != -EADDRINUSE)
			break;
	}
	return err;
}


static int
p9_fd_create_tcp(struct p9_client *client, const char *addr, char *args)
{
	int err;
	struct socket *csocket;
	struct sockaddr_in sin_server;
	struct p9_fd_opts opts;

	err = parse_opts(args, &opts);
	if (err < 0)
		return err;

	if (addr == NULL || valid_ipaddr4(addr) < 0)
		return -EINVAL;

	csocket = NULL;

	client->trans_opts.tcp.port = opts.port;
	client->trans_opts.tcp.privport = opts.privport;
	sin_server.sin_family = AF_INET;
	sin_server.sin_addr.s_addr = in_aton(addr);
	sin_server.sin_port = htons(opts.port);
	err = __sock_create(current->nsproxy->net_ns, PF_INET,
			    SOCK_STREAM, IPPROTO_TCP, &csocket, 1);
	if (err) {
		pr_err("%s (%d): problem creating socket\n",
		       __func__, task_pid_nr(current));
		return err;
	}

	if (opts.privport) {
		err = p9_bind_privport(csocket);
		if (err < 0) {
			pr_err("%s (%d): problem binding to privport\n",
			       __func__, task_pid_nr(current));
			sock_release(csocket);
			return err;
		}
	}

	err = csocket->ops->connect(csocket,
				    (struct sockaddr *)&sin_server,
				    sizeof(struct sockaddr_in), 0);
	if (err < 0) {
		pr_err("%s (%d): problem connecting socket to %s\n",
		       __func__, task_pid_nr(current), addr);
		sock_release(csocket);
		return err;
	}

	return p9_socket_open(client, csocket);
}

static int
p9_fd_create_unix(struct p9_client *client, const char *addr, char *args)
{
	int err;
	struct socket *csocket;
	struct sockaddr_un sun_server;

	csocket = NULL;

	if (!addr || !strlen(addr))
		return -EINVAL;

	if (strlen(addr) >= UNIX_PATH_MAX) {
		pr_err("%s (%d): address too long: %s\n",
		       __func__, task_pid_nr(current), addr);
		return -ENAMETOOLONG;
	}

	sun_server.sun_family = PF_UNIX;
	strcpy(sun_server.sun_path, addr);
	err = __sock_create(current->nsproxy->net_ns, PF_UNIX,
			    SOCK_STREAM, 0, &csocket, 1);
	if (err < 0) {
		pr_err("%s (%d): problem creating socket\n",
		       __func__, task_pid_nr(current));

		return err;
	}
	err = csocket->ops->connect(csocket, (struct sockaddr *)&sun_server,
			sizeof(struct sockaddr_un) - 1, 0);
	if (err < 0) {
		pr_err("%s (%d): problem connecting socket: %s: %d\n",
		       __func__, task_pid_nr(current), addr, err);
		sock_release(csocket);
		return err;
	}

	return p9_socket_open(client, csocket);
}

static int
p9_fd_create(struct p9_client *client, const char *addr, char *args)
{
	int err;
	struct p9_fd_opts opts;

	parse_opts(args, &opts);
	client->trans_opts.fd.rfd = opts.rfd;
	client->trans_opts.fd.wfd = opts.wfd;

	if (opts.rfd == ~0 || opts.wfd == ~0) {
		pr_err("Insufficient options for proto=fd\n");
		return -ENOPROTOOPT;
	}

	err = p9_fd_open(client, opts.rfd, opts.wfd);
	if (err < 0)
		return err;

	p9_conn_create(client);

	return 0;
}

static struct p9_trans_module p9_tcp_trans = {
	.name = "tcp",
	.maxsize = MAX_SOCK_BUF,
	.def = 0,
	.create = p9_fd_create_tcp,
	.close = p9_fd_close,
	.request = p9_fd_request,
	.cancel = p9_fd_cancel,
	.cancelled = p9_fd_cancelled,
	.show_options = p9_fd_show_options,
	.owner = THIS_MODULE,
};

static struct p9_trans_module p9_unix_trans = {
	.name = "unix",
	.maxsize = MAX_SOCK_BUF,
	.def = 0,
	.create = p9_fd_create_unix,
	.close = p9_fd_close,
	.request = p9_fd_request,
	.cancel = p9_fd_cancel,
	.cancelled = p9_fd_cancelled,
	.show_options = p9_fd_show_options,
	.owner = THIS_MODULE,
};

static struct p9_trans_module p9_fd_trans = {
	.name = "fd",
	.maxsize = MAX_SOCK_BUF,
	.def = 0,
	.create = p9_fd_create,
	.close = p9_fd_close,
	.request = p9_fd_request,
	.cancel = p9_fd_cancel,
	.cancelled = p9_fd_cancelled,
	.show_options = p9_fd_show_options,
	.owner = THIS_MODULE,
};

/**
 * p9_poll_workfn - poll worker thread
 * @work: work queue
 *
 * polls all v9fs transports for new events and queues the appropriate
 * work to the work queue
 *
 */

static void p9_poll_workfn(struct work_struct *work)
{
	unsigned long flags;

	p9_debug(P9_DEBUG_TRANS, "start %p\n", current);

	spin_lock_irqsave(&p9_poll_lock, flags);
	while (!list_empty(&p9_poll_pending_list)) {
		struct p9_conn *conn = list_first_entry(&p9_poll_pending_list,
							struct p9_conn,
							poll_pending_link);
		list_del_init(&conn->poll_pending_link);
		spin_unlock_irqrestore(&p9_poll_lock, flags);

		p9_poll_mux(conn);

		spin_lock_irqsave(&p9_poll_lock, flags);
	}
	spin_unlock_irqrestore(&p9_poll_lock, flags);

	p9_debug(P9_DEBUG_TRANS, "finish\n");
}

int p9_trans_fd_init(void)
{
	v9fs_register_trans(&p9_tcp_trans);
	v9fs_register_trans(&p9_unix_trans);
	v9fs_register_trans(&p9_fd_trans);

	return 0;
}

void p9_trans_fd_exit(void)
{
	flush_work(&p9_poll_work);
	v9fs_unregister_trans(&p9_tcp_trans);
	v9fs_unregister_trans(&p9_unix_trans);
	v9fs_unregister_trans(&p9_fd_trans);
}
