/*
 * net/9p/clnt.c
 *
 * 9P Client
 *
 *  Copyright (C) 2008 by Eric Van Hensbergen <ericvh@gmail.com>
 *  Copyright (C) 2007 by Latchesar Ionkov <lucho@ionkov.net>
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
#include <linux/idr.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <net/9p/9p.h>
#include <linux/parser.h>
#include <net/9p/client.h>
#include <net/9p/transport.h>

/*
  * Client Option Parsing (code inspired by NFS code)
  *  - a little lazy - parse all client options
  */

enum {
	Opt_msize,
	Opt_trans,
	Opt_legacy,
	Opt_err,
};

static const match_table_t tokens = {
	{Opt_msize, "msize=%u"},
	{Opt_legacy, "noextend"},
	{Opt_trans, "trans=%s"},
	{Opt_err, NULL},
};

static int
p9_client_rpc(struct p9_client *c, struct p9_fcall *tc, struct p9_fcall **rc);

/**
 * v9fs_parse_options - parse mount options into session structure
 * @options: options string passed from mount
 * @v9ses: existing v9fs session information
 *
 * Return 0 upon success, -ERRNO upon failure
 */

static int parse_opts(char *opts, struct p9_client *clnt)
{
	char *options;
	char *p;
	substring_t args[MAX_OPT_ARGS];
	int option;
	int ret = 0;

	clnt->dotu = 1;
	clnt->msize = 8192;

	if (!opts)
		return 0;

	options = kstrdup(opts, GFP_KERNEL);
	if (!options) {
		P9_DPRINTK(P9_DEBUG_ERROR,
				"failed to allocate copy of option string\n");
		return -ENOMEM;
	}

	while ((p = strsep(&options, ",")) != NULL) {
		int token;
		if (!*p)
			continue;
		token = match_token(p, tokens, args);
		if (token < Opt_trans) {
			int r = match_int(&args[0], &option);
			if (r < 0) {
				P9_DPRINTK(P9_DEBUG_ERROR,
					"integer field, but no integer?\n");
				ret = r;
				continue;
			}
		}
		switch (token) {
		case Opt_msize:
			clnt->msize = option;
			break;
		case Opt_trans:
			clnt->trans_mod = v9fs_get_trans_by_name(&args[0]);
			break;
		case Opt_legacy:
			clnt->dotu = 0;
			break;
		default:
			continue;
		}
	}

	if (!clnt->trans_mod)
		clnt->trans_mod = v9fs_get_default_trans();

	kfree(options);
	return ret;
}

/**
 * p9_tag_alloc - lookup/allocate a request by tag
 * @c: client session to lookup tag within
 * @tag: numeric id for transaction
 *
 * this is a simple array lookup, but will grow the
 * request_slots as necessary to accomodate transaction
 * ids which did not previously have a slot.
 *
 * this code relies on the client spinlock to manage locks, its
 * possible we should switch to something else, but I'd rather
 * stick with something low-overhead for the common case.
 *
 */

struct p9_req_t *p9_tag_alloc(struct p9_client *c, u16 tag)
{
	unsigned long flags;
	int row, col;

	/* This looks up the original request by tag so we know which
	 * buffer to read the data into */
	tag++;

	if (tag >= c->max_tag) {
		spin_lock_irqsave(&c->lock, flags);
		/* check again since original check was outside of lock */
		while (tag >= c->max_tag) {
			row = (tag / P9_ROW_MAXTAG);
			c->reqs[row] = kcalloc(P9_ROW_MAXTAG,
					sizeof(struct p9_req_t), GFP_ATOMIC);

			if (!c->reqs[row]) {
				printk(KERN_ERR "Couldn't grow tag array\n");
				BUG();
			}
			for (col = 0; col < P9_ROW_MAXTAG; col++) {
				c->reqs[row][col].status = REQ_STATUS_IDLE;
				c->reqs[row][col].flush_tag = P9_NOTAG;
				c->reqs[row][col].wq = kmalloc(
					sizeof(wait_queue_head_t), GFP_ATOMIC);
				if (!c->reqs[row][col].wq) {
					printk(KERN_ERR
						"Couldn't grow tag array\n");
					BUG();
				}
				init_waitqueue_head(c->reqs[row][col].wq);
			}
			c->max_tag += P9_ROW_MAXTAG;
		}
		spin_unlock_irqrestore(&c->lock, flags);
	}
	row = tag / P9_ROW_MAXTAG;
	col = tag % P9_ROW_MAXTAG;

	c->reqs[row][col].status = REQ_STATUS_ALLOC;
	c->reqs[row][col].flush_tag = P9_NOTAG;

	return &c->reqs[row][col];
}
EXPORT_SYMBOL(p9_tag_alloc);

/**
 * p9_tag_lookup - lookup a request by tag
 * @c: client session to lookup tag within
 * @tag: numeric id for transaction
 *
 */

struct p9_req_t *p9_tag_lookup(struct p9_client *c, u16 tag)
{
	int row, col;

	/* This looks up the original request by tag so we know which
	 * buffer to read the data into */
	tag++;

	BUG_ON(tag >= c->max_tag);

	row = tag / P9_ROW_MAXTAG;
	col = tag % P9_ROW_MAXTAG;

	return &c->reqs[row][col];
}
EXPORT_SYMBOL(p9_tag_lookup);

/**
 * p9_tag_init - setup tags structure and contents
 * @tags: tags structure from the client struct
 *
 * This initializes the tags structure for each client instance.
 *
 */

static int p9_tag_init(struct p9_client *c)
{
	int err = 0;

	c->tagpool = p9_idpool_create();
	if (IS_ERR(c->tagpool)) {
		err = PTR_ERR(c->tagpool);
		c->tagpool = NULL;
		goto error;
	}

	p9_idpool_get(c->tagpool); /* reserve tag 0 */

	c->max_tag = 0;
error:
	return err;
}

/**
 * p9_tag_cleanup - cleans up tags structure and reclaims resources
 * @tags: tags structure from the client struct
 *
 * This frees resources associated with the tags structure
 *
 */
static void p9_tag_cleanup(struct p9_client *c)
{
	int row, col;

	/* check to insure all requests are idle */
	for (row = 0; row < (c->max_tag/P9_ROW_MAXTAG); row++) {
		for (col = 0; col < P9_ROW_MAXTAG; col++) {
			if (c->reqs[row][col].status != REQ_STATUS_IDLE) {
				P9_DPRINTK(P9_DEBUG_MUX,
				  "Attempting to cleanup non-free tag %d,%d\n",
				  row, col);
				/* TODO: delay execution of cleanup */
				return;
			}
		}
	}

	if (c->tagpool)
		p9_idpool_destroy(c->tagpool);

	/* free requests associated with tags */
	for (row = 0; row < (c->max_tag/P9_ROW_MAXTAG); row++) {
		for (col = 0; col < P9_ROW_MAXTAG; col++)
			kfree(c->reqs[row][col].wq);
		kfree(c->reqs[row]);
	}
	c->max_tag = 0;
}

/**
 * p9_client_flush - flush (cancel) a request
 * c: client state
 * req: request to cancel
 *
 * This sents a flush for a particular requests and links
 * the flush request to the original request.  The current
 * code only supports a single flush request although the protocol
 * allows for multiple flush requests to be sent for a single request.
 *
 */

static int p9_client_flush(struct p9_client *c, struct p9_req_t *req)
{
	struct p9_fcall *tc, *rc = NULL;
	int err;

	P9_DPRINTK(P9_DEBUG_9P, "client %p tag %d\n", c, req->tc->tag);

	tc = p9_create_tflush(req->tc->tag);
	if (IS_ERR(tc))
		return PTR_ERR(tc);

	err = p9_client_rpc(c, tc, &rc);

	/* we don't free anything here because RPC isn't complete */

	return err;
}

/**
 * p9_free_req - free a request and clean-up as necessary
 * c: client state
 * r: request to release
 *
 */

void p9_free_req(struct p9_client *c, struct p9_req_t *r)
{
	r->flush_tag = P9_NOTAG;
	r->status = REQ_STATUS_IDLE;
	if (r->tc->tag != P9_NOTAG && p9_idpool_check(r->tc->tag, c->tagpool))
		p9_idpool_put(r->tc->tag, c->tagpool);

	/* if this was a flush request we have to free response fcall */
	if (r->tc->id == P9_TFLUSH) {
		kfree(r->tc);
		kfree(r->rc);
	}
}

/**
 * p9_client_cb - call back from transport to client
 * c: client state
 * req: request received
 *
 */
void p9_client_cb(struct p9_client *c, struct p9_req_t *req)
{
	struct p9_req_t *other_req;
	unsigned long flags;

	P9_DPRINTK(P9_DEBUG_MUX, ": %d\n", req->tc->tag);

	if (req->status == REQ_STATUS_ERROR)
		wake_up(req->wq);

	if (req->tc->id == P9_TFLUSH) { /* flush receive path */
		P9_DPRINTK(P9_DEBUG_MUX, "flush: %d\n", req->tc->tag);
		spin_lock_irqsave(&c->lock, flags);
		other_req = p9_tag_lookup(c, req->tc->params.tflush.oldtag);
		if (other_req->flush_tag != req->tc->tag) /* stale flush */
			spin_unlock_irqrestore(&c->lock, flags);
		else {
			BUG_ON(other_req->status != REQ_STATUS_FLSH);
			other_req->status = REQ_STATUS_FLSHD;
			spin_unlock_irqrestore(&c->lock, flags);
			wake_up(other_req->wq);
		}
		p9_free_req(c, req);
	} else { 				/* normal receive path */
		P9_DPRINTK(P9_DEBUG_MUX, "normal: %d\n", req->tc->tag);
		spin_lock_irqsave(&c->lock, flags);
		if (req->status != REQ_STATUS_FLSHD)
			req->status = REQ_STATUS_RCVD;
		req->flush_tag = P9_NOTAG;
		spin_unlock_irqrestore(&c->lock, flags);
		wake_up(req->wq);
		P9_DPRINTK(P9_DEBUG_MUX, "wakeup: %d\n", req->tc->tag);
	}
}
EXPORT_SYMBOL(p9_client_cb);

/**
 * p9_client_rpc - issue a request and wait for a response
 * @c: client session
 * @tc: &p9_fcall request to transmit
 * @rc: &p9_fcall to put reponse into
 *
 * Returns 0 on success, error code on failure
 */

static int
p9_client_rpc(struct p9_client *c, struct p9_fcall *tc, struct p9_fcall **rc)
{
	int tag, err, size;
	char *rdata;
	struct p9_req_t *req;
	unsigned long flags;
	int sigpending;
	int flushed = 0;

	P9_DPRINTK(P9_DEBUG_9P, "client %p tc %p rc %p\n", c, tc, rc);

	if (c->status != Connected)
		return -EIO;

	if (signal_pending(current)) {
		sigpending = 1;
		clear_thread_flag(TIF_SIGPENDING);
	} else
		sigpending = 0;

	tag = P9_NOTAG;
	if (tc->id != P9_TVERSION) {
		tag = p9_idpool_get(c->tagpool);
		if (tag < 0)
			return -ENOMEM;
	}

	req = p9_tag_alloc(c, tag);

	/* if this is a flush request, backlink flush request now to
	 * avoid race conditions later. */
	if (tc->id == P9_TFLUSH) {
		struct p9_req_t *other_req =
				p9_tag_lookup(c, tc->params.tflush.oldtag);
		if (other_req->status == REQ_STATUS_FLSH)
			other_req->flush_tag = tag;
	}

	p9_set_tag(tc, tag);

	/*
	 * if client passed in a pre-allocated response fcall struct
	 * then we just use that, otherwise we allocate one.
	 */

	if (rc == NULL)
		req->rc = NULL;
	else
		req->rc = *rc;
	if (req->rc == NULL) {
		req->rc = kmalloc(sizeof(struct p9_fcall) + c->msize,
								GFP_KERNEL);
		if (!req->rc) {
			err = -ENOMEM;
			p9_idpool_put(tag, c->tagpool);
			p9_free_req(c, req);
			goto reterr;
		}
		*rc = req->rc;
	}

	rdata = (char *)req->rc+sizeof(struct p9_fcall);

	req->tc = tc;
	P9_DPRINTK(P9_DEBUG_9P, "request: tc: %p rc: %p\n", req->tc, req->rc);

	err = c->trans_mod->request(c, req);
	if (err < 0) {
		c->status = Disconnected;
		goto reterr;
	}

	/* if it was a flush we just transmitted, return our tag */
	if (tc->id == P9_TFLUSH)
		return 0;
again:
	P9_DPRINTK(P9_DEBUG_9P, "wait %p tag: %d\n", req->wq, tag);
	err = wait_event_interruptible(*req->wq,
						req->status >= REQ_STATUS_RCVD);
	P9_DPRINTK(P9_DEBUG_9P, "wait %p tag: %d returned %d (flushed=%d)\n",
						req->wq, tag, err, flushed);

	if (req->status == REQ_STATUS_ERROR) {
		P9_DPRINTK(P9_DEBUG_9P, "req_status error %d\n", req->t_err);
		err = req->t_err;
	} else if (err == -ERESTARTSYS && flushed) {
		P9_DPRINTK(P9_DEBUG_9P, "flushed - going again\n");
		goto again;
	} else if (req->status == REQ_STATUS_FLSHD) {
		P9_DPRINTK(P9_DEBUG_9P, "flushed - erestartsys\n");
		err = -ERESTARTSYS;
	}

	if ((err == -ERESTARTSYS) && (c->status == Connected) && (!flushed)) {
		P9_DPRINTK(P9_DEBUG_9P, "flushing\n");
		spin_lock_irqsave(&c->lock, flags);
		if (req->status == REQ_STATUS_SENT)
			req->status = REQ_STATUS_FLSH;
		spin_unlock_irqrestore(&c->lock, flags);
		sigpending = 1;
		flushed = 1;
		clear_thread_flag(TIF_SIGPENDING);

		if (c->trans_mod->cancel(c, req)) {
			err = p9_client_flush(c, req);
			if (err == 0)
				goto again;
		}
	}

	if (sigpending) {
		spin_lock_irqsave(&current->sighand->siglock, flags);
		recalc_sigpending();
		spin_unlock_irqrestore(&current->sighand->siglock, flags);
	}

	if (err < 0)
		goto reterr;

	size = le32_to_cpu(*(__le32 *) rdata);

	err = p9_deserialize_fcall(rdata, size, req->rc, c->dotu);
	if (err < 0) {
		P9_DPRINTK(P9_DEBUG_9P,
			"9p debug: client rpc deserialize returned %d\n", err);
		goto reterr;
	}

	if (req->rc->id == P9_RERROR) {
		int ecode = req->rc->params.rerror.errno;
		struct p9_str *ename = &req->rc->params.rerror.error;

		P9_DPRINTK(P9_DEBUG_MUX, "Rerror %.*s\n", ename->len,
								ename->str);

		if (c->dotu)
			err = -ecode;

		if (!err) {
			err = p9_errstr2errno(ename->str, ename->len);

			/* string match failed */
			if (!err) {
				PRINT_FCALL_ERROR("unknown error", req->rc);
				err = -ESERVERFAULT;
			}
		}
	} else
		err = 0;

reterr:
	p9_free_req(c, req);

	P9_DPRINTK(P9_DEBUG_9P, "returning %d\n", err);
	return err;
}

static struct p9_fid *p9_fid_create(struct p9_client *clnt)
{
	int err;
	struct p9_fid *fid;

	P9_DPRINTK(P9_DEBUG_9P, "clnt %p\n", clnt);
	fid = kmalloc(sizeof(struct p9_fid), GFP_KERNEL);
	if (!fid)
		return ERR_PTR(-ENOMEM);

	fid->fid = p9_idpool_get(clnt->fidpool);
	if (fid->fid < 0) {
		err = -ENOSPC;
		goto error;
	}

	memset(&fid->qid, 0, sizeof(struct p9_qid));
	fid->mode = -1;
	fid->rdir_fpos = 0;
	fid->uid = current->fsuid;
	fid->clnt = clnt;
	fid->aux = NULL;

	spin_lock(&clnt->lock);
	list_add(&fid->flist, &clnt->fidlist);
	spin_unlock(&clnt->lock);

	return fid;

error:
	kfree(fid);
	return ERR_PTR(err);
}

static void p9_fid_destroy(struct p9_fid *fid)
{
	struct p9_client *clnt;

	P9_DPRINTK(P9_DEBUG_9P, "fid %d\n", fid->fid);
	clnt = fid->clnt;
	p9_idpool_put(fid->fid, clnt->fidpool);
	spin_lock(&clnt->lock);
	list_del(&fid->flist);
	spin_unlock(&clnt->lock);
	kfree(fid);
}

static int p9_client_version(struct p9_client *clnt)
{
	int err = 0;
	struct p9_fcall *tc, *rc;
	struct p9_str *version;

	P9_DPRINTK(P9_DEBUG_9P, "%p\n", clnt);
	err = 0;
	tc = NULL;
	rc = NULL;

	tc = p9_create_tversion(clnt->msize,
					clnt->dotu ? "9P2000.u" : "9P2000");
	if (IS_ERR(tc)) {
		err = PTR_ERR(tc);
		tc = NULL;
		goto error;
	}

	err = p9_client_rpc(clnt, tc, &rc);
	if (err)
		goto error;

	version = &rc->params.rversion.version;
	if (version->len == 8 && !memcmp(version->str, "9P2000.u", 8))
		clnt->dotu = 1;
	else if (version->len == 6 && !memcmp(version->str, "9P2000", 6))
		clnt->dotu = 0;
	else {
		err = -EREMOTEIO;
		goto error;
	}

	if (rc->params.rversion.msize < clnt->msize)
		clnt->msize = rc->params.rversion.msize;

error:
	kfree(tc);
	kfree(rc);

	return err;
}
EXPORT_SYMBOL(p9_client_auth);

struct p9_client *p9_client_create(const char *dev_name, char *options)
{
	int err;
	struct p9_client *clnt;

	err = 0;
	clnt = kmalloc(sizeof(struct p9_client), GFP_KERNEL);
	if (!clnt)
		return ERR_PTR(-ENOMEM);

	clnt->trans_mod = NULL;
	clnt->trans = NULL;
	spin_lock_init(&clnt->lock);
	INIT_LIST_HEAD(&clnt->fidlist);
	clnt->fidpool = p9_idpool_create();
	if (IS_ERR(clnt->fidpool)) {
		err = PTR_ERR(clnt->fidpool);
		clnt->fidpool = NULL;
		goto error;
	}

	p9_tag_init(clnt);

	err = parse_opts(options, clnt);
	if (err < 0)
		goto error;

	if (clnt->trans_mod == NULL) {
		err = -EPROTONOSUPPORT;
		P9_DPRINTK(P9_DEBUG_ERROR,
				"No transport defined or default transport\n");
		goto error;
	}

	P9_DPRINTK(P9_DEBUG_9P, "clnt %p trans %p msize %d dotu %d\n",
		clnt, clnt->trans_mod, clnt->msize, clnt->dotu);

	err = clnt->trans_mod->create(clnt, dev_name, options);
	if (err)
		goto error;

	if ((clnt->msize+P9_IOHDRSZ) > clnt->trans_mod->maxsize)
		clnt->msize = clnt->trans_mod->maxsize-P9_IOHDRSZ;

	err = p9_client_version(clnt);
	if (err)
		goto error;

	return clnt;

error:
	p9_client_destroy(clnt);
	return ERR_PTR(err);
}
EXPORT_SYMBOL(p9_client_create);

void p9_client_destroy(struct p9_client *clnt)
{
	struct p9_fid *fid, *fidptr;

	P9_DPRINTK(P9_DEBUG_9P, "clnt %p\n", clnt);

	if (clnt->trans_mod)
		clnt->trans_mod->close(clnt);

	v9fs_put_trans(clnt->trans_mod);

	list_for_each_entry_safe(fid, fidptr, &clnt->fidlist, flist)
		p9_fid_destroy(fid);

	if (clnt->fidpool)
		p9_idpool_destroy(clnt->fidpool);

	p9_tag_cleanup(clnt);

	kfree(clnt);
}
EXPORT_SYMBOL(p9_client_destroy);

void p9_client_disconnect(struct p9_client *clnt)
{
	P9_DPRINTK(P9_DEBUG_9P, "clnt %p\n", clnt);
	clnt->status = Disconnected;
}
EXPORT_SYMBOL(p9_client_disconnect);

struct p9_fid *p9_client_attach(struct p9_client *clnt, struct p9_fid *afid,
	char *uname, u32 n_uname, char *aname)
{
	int err;
	struct p9_fcall *tc, *rc;
	struct p9_fid *fid;

	P9_DPRINTK(P9_DEBUG_9P, "clnt %p afid %d uname %s aname %s\n",
		clnt, afid?afid->fid:-1, uname, aname);
	err = 0;
	tc = NULL;
	rc = NULL;

	fid = p9_fid_create(clnt);
	if (IS_ERR(fid)) {
		err = PTR_ERR(fid);
		fid = NULL;
		goto error;
	}

	tc = p9_create_tattach(fid->fid, afid?afid->fid:P9_NOFID, uname, aname,
		n_uname, clnt->dotu);
	if (IS_ERR(tc)) {
		err = PTR_ERR(tc);
		tc = NULL;
		goto error;
	}

	err = p9_client_rpc(clnt, tc, &rc);
	if (err)
		goto error;

	memmove(&fid->qid, &rc->params.rattach.qid, sizeof(struct p9_qid));
	kfree(tc);
	kfree(rc);
	return fid;

error:
	kfree(tc);
	kfree(rc);
	if (fid)
		p9_fid_destroy(fid);
	return ERR_PTR(err);
}
EXPORT_SYMBOL(p9_client_attach);

struct p9_fid *p9_client_auth(struct p9_client *clnt, char *uname,
	u32 n_uname, char *aname)
{
	int err;
	struct p9_fcall *tc, *rc;
	struct p9_fid *fid;

	P9_DPRINTK(P9_DEBUG_9P, "clnt %p uname %s aname %s\n", clnt, uname,
									aname);
	err = 0;
	tc = NULL;
	rc = NULL;

	fid = p9_fid_create(clnt);
	if (IS_ERR(fid)) {
		err = PTR_ERR(fid);
		fid = NULL;
		goto error;
	}

	tc = p9_create_tauth(fid->fid, uname, aname, n_uname, clnt->dotu);
	if (IS_ERR(tc)) {
		err = PTR_ERR(tc);
		tc = NULL;
		goto error;
	}

	err = p9_client_rpc(clnt, tc, &rc);
	if (err)
		goto error;

	memmove(&fid->qid, &rc->params.rauth.qid, sizeof(struct p9_qid));
	kfree(tc);
	kfree(rc);
	return fid;

error:
	kfree(tc);
	kfree(rc);
	if (fid)
		p9_fid_destroy(fid);
	return ERR_PTR(err);
}
EXPORT_SYMBOL(p9_client_auth);

struct p9_fid *p9_client_walk(struct p9_fid *oldfid, int nwname, char **wnames,
	int clone)
{
	int err;
	struct p9_fcall *tc, *rc;
	struct p9_client *clnt;
	struct p9_fid *fid;

	P9_DPRINTK(P9_DEBUG_9P, "fid %d nwname %d wname[0] %s\n",
		oldfid->fid, nwname, wnames?wnames[0]:NULL);
	err = 0;
	tc = NULL;
	rc = NULL;
	clnt = oldfid->clnt;
	if (clone) {
		fid = p9_fid_create(clnt);
		if (IS_ERR(fid)) {
			err = PTR_ERR(fid);
			fid = NULL;
			goto error;
		}

		fid->uid = oldfid->uid;
	} else
		fid = oldfid;

	tc = p9_create_twalk(oldfid->fid, fid->fid, nwname, wnames);
	if (IS_ERR(tc)) {
		err = PTR_ERR(tc);
		tc = NULL;
		goto error;
	}

	err = p9_client_rpc(clnt, tc, &rc);
	if (err) {
		if (rc && rc->id == P9_RWALK)
			goto clunk_fid;
		else
			goto error;
	}

	if (rc->params.rwalk.nwqid != nwname) {
		err = -ENOENT;
		goto clunk_fid;
	}

	if (nwname)
		memmove(&fid->qid,
			&rc->params.rwalk.wqids[rc->params.rwalk.nwqid - 1],
			sizeof(struct p9_qid));
	else
		fid->qid = oldfid->qid;

	kfree(tc);
	kfree(rc);
	return fid;

clunk_fid:
	kfree(tc);
	kfree(rc);
	rc = NULL;
	tc = p9_create_tclunk(fid->fid);
	if (IS_ERR(tc)) {
		err = PTR_ERR(tc);
		tc = NULL;
		goto error;
	}

	p9_client_rpc(clnt, tc, &rc);

error:
	kfree(tc);
	kfree(rc);
	if (fid && (fid != oldfid))
		p9_fid_destroy(fid);

	return ERR_PTR(err);
}
EXPORT_SYMBOL(p9_client_walk);

int p9_client_open(struct p9_fid *fid, int mode)
{
	int err;
	struct p9_fcall *tc, *rc;
	struct p9_client *clnt;

	P9_DPRINTK(P9_DEBUG_9P, "fid %d mode %d\n", fid->fid, mode);
	err = 0;
	tc = NULL;
	rc = NULL;
	clnt = fid->clnt;

	if (fid->mode != -1)
		return -EINVAL;

	tc = p9_create_topen(fid->fid, mode);
	if (IS_ERR(tc)) {
		err = PTR_ERR(tc);
		tc = NULL;
		goto done;
	}

	err = p9_client_rpc(clnt, tc, &rc);
	if (err)
		goto done;

	fid->mode = mode;
	fid->iounit = rc->params.ropen.iounit;

done:
	kfree(tc);
	kfree(rc);
	return err;
}
EXPORT_SYMBOL(p9_client_open);

int p9_client_fcreate(struct p9_fid *fid, char *name, u32 perm, int mode,
		     char *extension)
{
	int err;
	struct p9_fcall *tc, *rc;
	struct p9_client *clnt;

	P9_DPRINTK(P9_DEBUG_9P, "fid %d name %s perm %d mode %d\n", fid->fid,
		name, perm, mode);
	err = 0;
	tc = NULL;
	rc = NULL;
	clnt = fid->clnt;

	if (fid->mode != -1)
		return -EINVAL;

	tc = p9_create_tcreate(fid->fid, name, perm, mode, extension,
							       clnt->dotu);
	if (IS_ERR(tc)) {
		err = PTR_ERR(tc);
		tc = NULL;
		goto done;
	}

	err = p9_client_rpc(clnt, tc, &rc);
	if (err)
		goto done;

	fid->mode = mode;
	fid->iounit = rc->params.ropen.iounit;

done:
	kfree(tc);
	kfree(rc);
	return err;
}
EXPORT_SYMBOL(p9_client_fcreate);

int p9_client_clunk(struct p9_fid *fid)
{
	int err;
	struct p9_fcall *tc, *rc;
	struct p9_client *clnt;

	P9_DPRINTK(P9_DEBUG_9P, "fid %d\n", fid->fid);
	err = 0;
	tc = NULL;
	rc = NULL;
	clnt = fid->clnt;

	tc = p9_create_tclunk(fid->fid);
	if (IS_ERR(tc)) {
		err = PTR_ERR(tc);
		tc = NULL;
		goto done;
	}

	err = p9_client_rpc(clnt, tc, &rc);
	if (err)
		goto done;

	p9_fid_destroy(fid);

done:
	kfree(tc);
	kfree(rc);
	return err;
}
EXPORT_SYMBOL(p9_client_clunk);

int p9_client_remove(struct p9_fid *fid)
{
	int err;
	struct p9_fcall *tc, *rc;
	struct p9_client *clnt;

	P9_DPRINTK(P9_DEBUG_9P, "fid %d\n", fid->fid);
	err = 0;
	tc = NULL;
	rc = NULL;
	clnt = fid->clnt;

	tc = p9_create_tremove(fid->fid);
	if (IS_ERR(tc)) {
		err = PTR_ERR(tc);
		tc = NULL;
		goto done;
	}

	err = p9_client_rpc(clnt, tc, &rc);
	if (err)
		goto done;

	p9_fid_destroy(fid);

done:
	kfree(tc);
	kfree(rc);
	return err;
}
EXPORT_SYMBOL(p9_client_remove);

int
p9_client_read(struct p9_fid *fid, char *data, char __user *udata, u64 offset,
								u32 count)
{
	int err, n, rsize, total;
	struct p9_fcall *tc, *rc;
	struct p9_client *clnt;

	P9_DPRINTK(P9_DEBUG_9P, "fid %d offset %llu %d\n", fid->fid,
					(long long unsigned) offset, count);
	err = 0;
	tc = NULL;
	rc = NULL;
	clnt = fid->clnt;
	total = 0;

	rsize = fid->iounit;
	if (!rsize || rsize > clnt->msize-P9_IOHDRSZ)
		rsize = clnt->msize - P9_IOHDRSZ;

	do {
		if (count < rsize)
			rsize = count;

		tc = p9_create_tread(fid->fid, offset, rsize);
		if (IS_ERR(tc)) {
			err = PTR_ERR(tc);
			tc = NULL;
			goto error;
		}

		err = p9_client_rpc(clnt, tc, &rc);
		if (err)
			goto error;

		n = rc->params.rread.count;
		if (n > count)
			n = count;

		if (data) {
			memmove(data, rc->params.rread.data, n);
			data += n;
		}

		if (udata) {
			err = copy_to_user(udata, rc->params.rread.data, n);
			if (err) {
				err = -EFAULT;
				goto error;
			}
			udata += n;
		}

		count -= n;
		offset += n;
		total += n;
		kfree(tc);
		tc = NULL;
		kfree(rc);
		rc = NULL;
	} while (count > 0 && n == rsize);

	return total;

error:
	kfree(tc);
	kfree(rc);
	return err;
}
EXPORT_SYMBOL(p9_client_read);

int
p9_client_write(struct p9_fid *fid, char *data, const char __user *udata,
							u64 offset, u32 count)
{
	int err, n, rsize, total;
	struct p9_fcall *tc, *rc;
	struct p9_client *clnt;

	P9_DPRINTK(P9_DEBUG_9P, "fid %d offset %llu count %d\n", fid->fid,
					(long long unsigned) offset, count);
	err = 0;
	tc = NULL;
	rc = NULL;
	clnt = fid->clnt;
	total = 0;

	rsize = fid->iounit;
	if (!rsize || rsize > clnt->msize-P9_IOHDRSZ)
		rsize = clnt->msize - P9_IOHDRSZ;

	do {
		if (count < rsize)
			rsize = count;

		if (data)
			tc = p9_create_twrite(fid->fid, offset, rsize, data);
		else
			tc = p9_create_twrite_u(fid->fid, offset, rsize, udata);
		if (IS_ERR(tc)) {
			err = PTR_ERR(tc);
			tc = NULL;
			goto error;
		}

		err = p9_client_rpc(clnt, tc, &rc);
		if (err)
			goto error;

		n = rc->params.rread.count;
		count -= n;

		if (data)
			data += n;
		else
			udata += n;

		offset += n;
		total += n;
		kfree(tc);
		tc = NULL;
		kfree(rc);
		rc = NULL;
	} while (count > 0);

	return total;

error:
	kfree(tc);
	kfree(rc);
	return err;
}
EXPORT_SYMBOL(p9_client_write);

static struct p9_stat *p9_clone_stat(struct p9_stat *st, int dotu)
{
	int n;
	char *p;
	struct p9_stat *ret;

	n = sizeof(struct p9_stat) + st->name.len + st->uid.len + st->gid.len +
		st->muid.len;

	if (dotu)
		n += st->extension.len;

	ret = kmalloc(n, GFP_KERNEL);
	if (!ret)
		return ERR_PTR(-ENOMEM);

	memmove(ret, st, sizeof(struct p9_stat));
	p = ((char *) ret) + sizeof(struct p9_stat);
	memmove(p, st->name.str, st->name.len);
	ret->name.str = p;
	p += st->name.len;
	memmove(p, st->uid.str, st->uid.len);
	ret->uid.str = p;
	p += st->uid.len;
	memmove(p, st->gid.str, st->gid.len);
	ret->gid.str = p;
	p += st->gid.len;
	memmove(p, st->muid.str, st->muid.len);
	ret->muid.str = p;
	p += st->muid.len;

	if (dotu) {
		memmove(p, st->extension.str, st->extension.len);
		ret->extension.str = p;
		p += st->extension.len;
	}

	return ret;
}

struct p9_stat *p9_client_stat(struct p9_fid *fid)
{
	int err;
	struct p9_fcall *tc, *rc;
	struct p9_client *clnt;
	struct p9_stat *ret;

	P9_DPRINTK(P9_DEBUG_9P, "fid %d\n", fid->fid);
	err = 0;
	tc = NULL;
	rc = NULL;
	ret = NULL;
	clnt = fid->clnt;

	tc = p9_create_tstat(fid->fid);
	if (IS_ERR(tc)) {
		err = PTR_ERR(tc);
		tc = NULL;
		goto error;
	}

	err = p9_client_rpc(clnt, tc, &rc);
	if (err)
		goto error;

	ret = p9_clone_stat(&rc->params.rstat.stat, clnt->dotu);
	if (IS_ERR(ret)) {
		err = PTR_ERR(ret);
		ret = NULL;
		goto error;
	}

	kfree(tc);
	kfree(rc);
	return ret;

error:
	kfree(tc);
	kfree(rc);
	kfree(ret);
	return ERR_PTR(err);
}
EXPORT_SYMBOL(p9_client_stat);

int p9_client_wstat(struct p9_fid *fid, struct p9_wstat *wst)
{
	int err;
	struct p9_fcall *tc, *rc;
	struct p9_client *clnt;

	P9_DPRINTK(P9_DEBUG_9P, "fid %d\n", fid->fid);
	err = 0;
	tc = NULL;
	rc = NULL;
	clnt = fid->clnt;

	tc = p9_create_twstat(fid->fid, wst, clnt->dotu);
	if (IS_ERR(tc)) {
		err = PTR_ERR(tc);
		tc = NULL;
		goto done;
	}

	err = p9_client_rpc(clnt, tc, &rc);

done:
	kfree(tc);
	kfree(rc);
	return err;
}
EXPORT_SYMBOL(p9_client_wstat);
