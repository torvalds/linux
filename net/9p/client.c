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
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <net/9p/9p.h>
#include <linux/parser.h>
#include <net/9p/client.h>
#include <net/9p/transport.h>
#include "protocol.h"

/*
  * Client Option Parsing (code inspired by NFS code)
  *  - a little lazy - parse all client options
  */

enum {
	Opt_msize,
	Opt_trans,
	Opt_legacy,
	Opt_version,
	Opt_err,
};

static const match_table_t tokens = {
	{Opt_msize, "msize=%u"},
	{Opt_legacy, "noextend"},
	{Opt_trans, "trans=%s"},
	{Opt_version, "version=%s"},
	{Opt_err, NULL},
};

inline int p9_is_proto_dotl(struct p9_client *clnt)
{
	return (clnt->proto_version == p9_proto_2000L);
}
EXPORT_SYMBOL(p9_is_proto_dotl);

inline int p9_is_proto_dotu(struct p9_client *clnt)
{
	return (clnt->proto_version == p9_proto_2000u);
}
EXPORT_SYMBOL(p9_is_proto_dotu);

/* Interpret mount option for protocol version */
static int get_protocol_version(const substring_t *name)
{
	int version = -EINVAL;

	if (!strncmp("9p2000", name->from, name->to-name->from)) {
		version = p9_proto_legacy;
		P9_DPRINTK(P9_DEBUG_9P, "Protocol version: Legacy\n");
	} else if (!strncmp("9p2000.u", name->from, name->to-name->from)) {
		version = p9_proto_2000u;
		P9_DPRINTK(P9_DEBUG_9P, "Protocol version: 9P2000.u\n");
	} else if (!strncmp("9p2000.L", name->from, name->to-name->from)) {
		version = p9_proto_2000L;
		P9_DPRINTK(P9_DEBUG_9P, "Protocol version: 9P2000.L\n");
	} else {
		P9_DPRINTK(P9_DEBUG_ERROR, "Unknown protocol version %s. ",
							name->from);
	}
	return version;
}

static struct p9_req_t *
p9_client_rpc(struct p9_client *c, int8_t type, const char *fmt, ...);

/**
 * parse_options - parse mount options into client structure
 * @opts: options string passed from mount
 * @clnt: existing v9fs client information
 *
 * Return 0 upon success, -ERRNO upon failure
 */

static int parse_opts(char *opts, struct p9_client *clnt)
{
	char *options, *tmp_options;
	char *p;
	substring_t args[MAX_OPT_ARGS];
	int option;
	int ret = 0;

	clnt->proto_version = p9_proto_2000u;
	clnt->msize = 8192;

	if (!opts)
		return 0;

	tmp_options = kstrdup(opts, GFP_KERNEL);
	if (!tmp_options) {
		P9_DPRINTK(P9_DEBUG_ERROR,
				"failed to allocate copy of option string\n");
		return -ENOMEM;
	}
	options = tmp_options;

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
			if(clnt->trans_mod == NULL) {
				P9_DPRINTK(P9_DEBUG_ERROR,
				   "Could not find request transport: %s\n",
				   (char *) &args[0]);
				ret = -EINVAL;
				goto free_and_return;
			}
			break;
		case Opt_legacy:
			clnt->proto_version = p9_proto_legacy;
			break;
		case Opt_version:
			ret = get_protocol_version(&args[0]);
			if (ret == -EINVAL)
				goto free_and_return;
			clnt->proto_version = ret;
			break;
		default:
			continue;
		}
	}

free_and_return:
	kfree(tmp_options);
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

static struct p9_req_t *p9_tag_alloc(struct p9_client *c, u16 tag)
{
	unsigned long flags;
	int row, col;
	struct p9_req_t *req;

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
				spin_unlock_irqrestore(&c->lock, flags);
				return ERR_PTR(-ENOMEM);
			}
			for (col = 0; col < P9_ROW_MAXTAG; col++) {
				c->reqs[row][col].status = REQ_STATUS_IDLE;
				c->reqs[row][col].tc = NULL;
			}
			c->max_tag += P9_ROW_MAXTAG;
		}
		spin_unlock_irqrestore(&c->lock, flags);
	}
	row = tag / P9_ROW_MAXTAG;
	col = tag % P9_ROW_MAXTAG;

	req = &c->reqs[row][col];
	if (!req->tc) {
		req->wq = kmalloc(sizeof(wait_queue_head_t), GFP_KERNEL);
		if (!req->wq) {
			printk(KERN_ERR "Couldn't grow tag array\n");
			return ERR_PTR(-ENOMEM);
		}
		init_waitqueue_head(req->wq);
		req->tc = kmalloc(sizeof(struct p9_fcall)+c->msize,
								GFP_KERNEL);
		req->rc = kmalloc(sizeof(struct p9_fcall)+c->msize,
								GFP_KERNEL);
		if ((!req->tc) || (!req->rc)) {
			printk(KERN_ERR "Couldn't grow tag array\n");
			kfree(req->tc);
			kfree(req->rc);
			kfree(req->wq);
			req->tc = req->rc = NULL;
			req->wq = NULL;
			return ERR_PTR(-ENOMEM);
		}
		req->tc->sdata = (char *) req->tc + sizeof(struct p9_fcall);
		req->tc->capacity = c->msize;
		req->rc->sdata = (char *) req->rc + sizeof(struct p9_fcall);
		req->rc->capacity = c->msize;
	}

	p9pdu_reset(req->tc);
	p9pdu_reset(req->rc);

	req->tc->tag = tag-1;
	req->status = REQ_STATUS_ALLOC;

	return &c->reqs[row][col];
}

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
 * @c:  v9fs client struct
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
 * @c:  v9fs client struct
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
		for (col = 0; col < P9_ROW_MAXTAG; col++) {
			kfree(c->reqs[row][col].wq);
			kfree(c->reqs[row][col].tc);
			kfree(c->reqs[row][col].rc);
		}
		kfree(c->reqs[row]);
	}
	c->max_tag = 0;
}

/**
 * p9_free_req - free a request and clean-up as necessary
 * c: client state
 * r: request to release
 *
 */

static void p9_free_req(struct p9_client *c, struct p9_req_t *r)
{
	int tag = r->tc->tag;
	P9_DPRINTK(P9_DEBUG_MUX, "clnt %p req %p tag: %d\n", c, r, tag);

	r->status = REQ_STATUS_IDLE;
	if (tag != P9_NOTAG && p9_idpool_check(tag, c->tagpool))
		p9_idpool_put(tag, c->tagpool);
}

/**
 * p9_client_cb - call back from transport to client
 * c: client state
 * req: request received
 *
 */
void p9_client_cb(struct p9_client *c, struct p9_req_t *req)
{
	P9_DPRINTK(P9_DEBUG_MUX, " tag %d\n", req->tc->tag);
	wake_up(req->wq);
	P9_DPRINTK(P9_DEBUG_MUX, "wakeup: %d\n", req->tc->tag);
}
EXPORT_SYMBOL(p9_client_cb);

/**
 * p9_parse_header - parse header arguments out of a packet
 * @pdu: packet to parse
 * @size: size of packet
 * @type: type of request
 * @tag: tag of packet
 * @rewind: set if we need to rewind offset afterwards
 */

int
p9_parse_header(struct p9_fcall *pdu, int32_t *size, int8_t *type, int16_t *tag,
								int rewind)
{
	int8_t r_type;
	int16_t r_tag;
	int32_t r_size;
	int offset = pdu->offset;
	int err;

	pdu->offset = 0;
	if (pdu->size == 0)
		pdu->size = 7;

	err = p9pdu_readf(pdu, 0, "dbw", &r_size, &r_type, &r_tag);
	if (err)
		goto rewind_and_exit;

	pdu->size = r_size;
	pdu->id = r_type;
	pdu->tag = r_tag;

	P9_DPRINTK(P9_DEBUG_9P, "<<< size=%d type: %d tag: %d\n", pdu->size,
							pdu->id, pdu->tag);

	if (type)
		*type = r_type;
	if (tag)
		*tag = r_tag;
	if (size)
		*size = r_size;


rewind_and_exit:
	if (rewind)
		pdu->offset = offset;
	return err;
}
EXPORT_SYMBOL(p9_parse_header);

/**
 * p9_check_errors - check 9p packet for error return and process it
 * @c: current client instance
 * @req: request to parse and check for error conditions
 *
 * returns error code if one is discovered, otherwise returns 0
 *
 * this will have to be more complicated if we have multiple
 * error packet types
 */

static int p9_check_errors(struct p9_client *c, struct p9_req_t *req)
{
	int8_t type;
	int err;

	err = p9_parse_header(req->rc, NULL, &type, NULL, 0);
	if (err) {
		P9_DPRINTK(P9_DEBUG_ERROR, "couldn't parse header %d\n", err);
		return err;
	}

	if (type == P9_RERROR) {
		int ecode;
		char *ename;

		err = p9pdu_readf(req->rc, c->proto_version, "s?d",
							&ename, &ecode);
		if (err) {
			P9_DPRINTK(P9_DEBUG_ERROR, "couldn't parse error%d\n",
									err);
			return err;
		}

		if (p9_is_proto_dotu(c))
			err = -ecode;

		if (!err || !IS_ERR_VALUE(err))
			err = p9_errstr2errno(ename, strlen(ename));

		P9_DPRINTK(P9_DEBUG_9P, "<<< RERROR (%d) %s\n", -ecode, ename);

		kfree(ename);
	} else
		err = 0;

	return err;
}

/**
 * p9_client_flush - flush (cancel) a request
 * @c: client state
 * @oldreq: request to cancel
 *
 * This sents a flush for a particular requests and links
 * the flush request to the original request.  The current
 * code only supports a single flush request although the protocol
 * allows for multiple flush requests to be sent for a single request.
 *
 */

static int p9_client_flush(struct p9_client *c, struct p9_req_t *oldreq)
{
	struct p9_req_t *req;
	int16_t oldtag;
	int err;

	err = p9_parse_header(oldreq->tc, NULL, NULL, &oldtag, 1);
	if (err)
		return err;

	P9_DPRINTK(P9_DEBUG_9P, ">>> TFLUSH tag %d\n", oldtag);

	req = p9_client_rpc(c, P9_TFLUSH, "w", oldtag);
	if (IS_ERR(req))
		return PTR_ERR(req);


	/* if we haven't received a response for oldreq,
	   remove it from the list. */
	spin_lock(&c->lock);
	if (oldreq->status == REQ_STATUS_FLSH)
		list_del(&oldreq->req_list);
	spin_unlock(&c->lock);

	p9_free_req(c, req);
	return 0;
}

/**
 * p9_client_rpc - issue a request and wait for a response
 * @c: client session
 * @type: type of request
 * @fmt: protocol format string (see protocol.c)
 *
 * Returns request structure (which client must free using p9_free_req)
 */

static struct p9_req_t *
p9_client_rpc(struct p9_client *c, int8_t type, const char *fmt, ...)
{
	va_list ap;
	int tag, err;
	struct p9_req_t *req;
	unsigned long flags;
	int sigpending;

	P9_DPRINTK(P9_DEBUG_MUX, "client %p op %d\n", c, type);

	/* we allow for any status other than disconnected */
	if (c->status == Disconnected)
		return ERR_PTR(-EIO);

	/* if status is begin_disconnected we allow only clunk request */
	if ((c->status == BeginDisconnect) && (type != P9_TCLUNK))
		return ERR_PTR(-EIO);

	if (signal_pending(current)) {
		sigpending = 1;
		clear_thread_flag(TIF_SIGPENDING);
	} else
		sigpending = 0;

	tag = P9_NOTAG;
	if (type != P9_TVERSION) {
		tag = p9_idpool_get(c->tagpool);
		if (tag < 0)
			return ERR_PTR(-ENOMEM);
	}

	req = p9_tag_alloc(c, tag);
	if (IS_ERR(req))
		return req;

	/* marshall the data */
	p9pdu_prepare(req->tc, tag, type);
	va_start(ap, fmt);
	err = p9pdu_vwritef(req->tc, c->proto_version, fmt, ap);
	va_end(ap);
	p9pdu_finalize(req->tc);

	err = c->trans_mod->request(c, req);
	if (err < 0) {
		c->status = Disconnected;
		goto reterr;
	}

	P9_DPRINTK(P9_DEBUG_MUX, "wait %p tag: %d\n", req->wq, tag);
	err = wait_event_interruptible(*req->wq,
						req->status >= REQ_STATUS_RCVD);
	P9_DPRINTK(P9_DEBUG_MUX, "wait %p tag: %d returned %d\n",
						req->wq, tag, err);

	if (req->status == REQ_STATUS_ERROR) {
		P9_DPRINTK(P9_DEBUG_ERROR, "req_status error %d\n", req->t_err);
		err = req->t_err;
	}

	if ((err == -ERESTARTSYS) && (c->status == Connected)) {
		P9_DPRINTK(P9_DEBUG_MUX, "flushing\n");
		sigpending = 1;
		clear_thread_flag(TIF_SIGPENDING);

		if (c->trans_mod->cancel(c, req))
			p9_client_flush(c, req);

		/* if we received the response anyway, don't signal error */
		if (req->status == REQ_STATUS_RCVD)
			err = 0;
	}

	if (sigpending) {
		spin_lock_irqsave(&current->sighand->siglock, flags);
		recalc_sigpending();
		spin_unlock_irqrestore(&current->sighand->siglock, flags);
	}

	if (err < 0)
		goto reterr;

	err = p9_check_errors(c, req);
	if (!err) {
		P9_DPRINTK(P9_DEBUG_MUX, "exit: client %p op %d\n", c, type);
		return req;
	}

reterr:
	P9_DPRINTK(P9_DEBUG_MUX, "exit: client %p op %d error: %d\n", c, type,
									err);
	p9_free_req(c, req);
	return ERR_PTR(err);
}

static struct p9_fid *p9_fid_create(struct p9_client *clnt)
{
	int ret;
	struct p9_fid *fid;
	unsigned long flags;

	P9_DPRINTK(P9_DEBUG_FID, "clnt %p\n", clnt);
	fid = kmalloc(sizeof(struct p9_fid), GFP_KERNEL);
	if (!fid)
		return ERR_PTR(-ENOMEM);

	ret = p9_idpool_get(clnt->fidpool);
	if (ret < 0) {
		ret = -ENOSPC;
		goto error;
	}
	fid->fid = ret;

	memset(&fid->qid, 0, sizeof(struct p9_qid));
	fid->mode = -1;
	fid->uid = current_fsuid();
	fid->clnt = clnt;
	fid->rdir = NULL;
	spin_lock_irqsave(&clnt->lock, flags);
	list_add(&fid->flist, &clnt->fidlist);
	spin_unlock_irqrestore(&clnt->lock, flags);

	return fid;

error:
	kfree(fid);
	return ERR_PTR(ret);
}

static void p9_fid_destroy(struct p9_fid *fid)
{
	struct p9_client *clnt;
	unsigned long flags;

	P9_DPRINTK(P9_DEBUG_FID, "fid %d\n", fid->fid);
	clnt = fid->clnt;
	p9_idpool_put(fid->fid, clnt->fidpool);
	spin_lock_irqsave(&clnt->lock, flags);
	list_del(&fid->flist);
	spin_unlock_irqrestore(&clnt->lock, flags);
	kfree(fid->rdir);
	kfree(fid);
}

int p9_client_version(struct p9_client *c)
{
	int err = 0;
	struct p9_req_t *req;
	char *version;
	int msize;

	P9_DPRINTK(P9_DEBUG_9P, ">>> TVERSION msize %d protocol %d\n",
						c->msize, c->proto_version);

	switch (c->proto_version) {
	case p9_proto_2000L:
		req = p9_client_rpc(c, P9_TVERSION, "ds",
					c->msize, "9P2000.L");
		break;
	case p9_proto_2000u:
		req = p9_client_rpc(c, P9_TVERSION, "ds",
					c->msize, "9P2000.u");
		break;
	case p9_proto_legacy:
		req = p9_client_rpc(c, P9_TVERSION, "ds",
					c->msize, "9P2000");
		break;
	default:
		return -EINVAL;
		break;
	}

	if (IS_ERR(req))
		return PTR_ERR(req);

	err = p9pdu_readf(req->rc, c->proto_version, "ds", &msize, &version);
	if (err) {
		P9_DPRINTK(P9_DEBUG_9P, "version error %d\n", err);
		p9pdu_dump(1, req->rc);
		goto error;
	}

	P9_DPRINTK(P9_DEBUG_9P, "<<< RVERSION msize %d %s\n", msize, version);
	if (!strncmp(version, "9P2000.L", 8))
		c->proto_version = p9_proto_2000L;
	else if (!strncmp(version, "9P2000.u", 8))
		c->proto_version = p9_proto_2000u;
	else if (!strncmp(version, "9P2000", 6))
		c->proto_version = p9_proto_legacy;
	else {
		err = -EREMOTEIO;
		goto error;
	}

	if (msize < c->msize)
		c->msize = msize;

error:
	kfree(version);
	p9_free_req(c, req);

	return err;
}
EXPORT_SYMBOL(p9_client_version);

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

	p9_tag_init(clnt);

	err = parse_opts(options, clnt);
	if (err < 0)
		goto free_client;

	if (!clnt->trans_mod)
		clnt->trans_mod = v9fs_get_default_trans();

	if (clnt->trans_mod == NULL) {
		err = -EPROTONOSUPPORT;
		P9_DPRINTK(P9_DEBUG_ERROR,
				"No transport defined or default transport\n");
		goto free_client;
	}

	clnt->fidpool = p9_idpool_create();
	if (IS_ERR(clnt->fidpool)) {
		err = PTR_ERR(clnt->fidpool);
		clnt->fidpool = NULL;
		goto put_trans;
	}

	P9_DPRINTK(P9_DEBUG_MUX, "clnt %p trans %p msize %d protocol %d\n",
		clnt, clnt->trans_mod, clnt->msize, clnt->proto_version);

	err = clnt->trans_mod->create(clnt, dev_name, options);
	if (err)
		goto destroy_fidpool;

	if ((clnt->msize+P9_IOHDRSZ) > clnt->trans_mod->maxsize)
		clnt->msize = clnt->trans_mod->maxsize-P9_IOHDRSZ;

	err = p9_client_version(clnt);
	if (err)
		goto close_trans;

	return clnt;

close_trans:
	clnt->trans_mod->close(clnt);
destroy_fidpool:
	p9_idpool_destroy(clnt->fidpool);
put_trans:
	v9fs_put_trans(clnt->trans_mod);
free_client:
	kfree(clnt);
	return ERR_PTR(err);
}
EXPORT_SYMBOL(p9_client_create);

void p9_client_destroy(struct p9_client *clnt)
{
	struct p9_fid *fid, *fidptr;

	P9_DPRINTK(P9_DEBUG_MUX, "clnt %p\n", clnt);

	if (clnt->trans_mod)
		clnt->trans_mod->close(clnt);

	v9fs_put_trans(clnt->trans_mod);

	list_for_each_entry_safe(fid, fidptr, &clnt->fidlist, flist) {
		printk(KERN_INFO "Found fid %d not clunked\n", fid->fid);
		p9_fid_destroy(fid);
	}

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

void p9_client_begin_disconnect(struct p9_client *clnt)
{
	P9_DPRINTK(P9_DEBUG_9P, "clnt %p\n", clnt);
	clnt->status = BeginDisconnect;
}
EXPORT_SYMBOL(p9_client_begin_disconnect);

struct p9_fid *p9_client_attach(struct p9_client *clnt, struct p9_fid *afid,
	char *uname, u32 n_uname, char *aname)
{
	int err;
	struct p9_req_t *req;
	struct p9_fid *fid;
	struct p9_qid qid;

	P9_DPRINTK(P9_DEBUG_9P, ">>> TATTACH afid %d uname %s aname %s\n",
					afid ? afid->fid : -1, uname, aname);
	err = 0;

	fid = p9_fid_create(clnt);
	if (IS_ERR(fid)) {
		err = PTR_ERR(fid);
		fid = NULL;
		goto error;
	}

	req = p9_client_rpc(clnt, P9_TATTACH, "ddss?d", fid->fid,
			afid ? afid->fid : P9_NOFID, uname, aname, n_uname);
	if (IS_ERR(req)) {
		err = PTR_ERR(req);
		goto error;
	}

	err = p9pdu_readf(req->rc, clnt->proto_version, "Q", &qid);
	if (err) {
		p9pdu_dump(1, req->rc);
		p9_free_req(clnt, req);
		goto error;
	}

	P9_DPRINTK(P9_DEBUG_9P, "<<< RATTACH qid %x.%llx.%x\n",
					qid.type,
					(unsigned long long)qid.path,
					qid.version);

	memmove(&fid->qid, &qid, sizeof(struct p9_qid));

	p9_free_req(clnt, req);
	return fid;

error:
	if (fid)
		p9_fid_destroy(fid);
	return ERR_PTR(err);
}
EXPORT_SYMBOL(p9_client_attach);

struct p9_fid *
p9_client_auth(struct p9_client *clnt, char *uname, u32 n_uname, char *aname)
{
	int err;
	struct p9_req_t *req;
	struct p9_qid qid;
	struct p9_fid *afid;

	P9_DPRINTK(P9_DEBUG_9P, ">>> TAUTH uname %s aname %s\n", uname, aname);
	err = 0;

	afid = p9_fid_create(clnt);
	if (IS_ERR(afid)) {
		err = PTR_ERR(afid);
		afid = NULL;
		goto error;
	}

	req = p9_client_rpc(clnt, P9_TAUTH, "dss?d",
			afid ? afid->fid : P9_NOFID, uname, aname, n_uname);
	if (IS_ERR(req)) {
		err = PTR_ERR(req);
		goto error;
	}

	err = p9pdu_readf(req->rc, clnt->proto_version, "Q", &qid);
	if (err) {
		p9pdu_dump(1, req->rc);
		p9_free_req(clnt, req);
		goto error;
	}

	P9_DPRINTK(P9_DEBUG_9P, "<<< RAUTH qid %x.%llx.%x\n",
					qid.type,
					(unsigned long long)qid.path,
					qid.version);

	memmove(&afid->qid, &qid, sizeof(struct p9_qid));
	p9_free_req(clnt, req);
	return afid;

error:
	if (afid)
		p9_fid_destroy(afid);
	return ERR_PTR(err);
}
EXPORT_SYMBOL(p9_client_auth);

struct p9_fid *p9_client_walk(struct p9_fid *oldfid, int nwname, char **wnames,
	int clone)
{
	int err;
	struct p9_client *clnt;
	struct p9_fid *fid;
	struct p9_qid *wqids;
	struct p9_req_t *req;
	int16_t nwqids, count;

	err = 0;
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


	P9_DPRINTK(P9_DEBUG_9P, ">>> TWALK fids %d,%d nwname %d wname[0] %s\n",
		oldfid->fid, fid->fid, nwname, wnames ? wnames[0] : NULL);

	req = p9_client_rpc(clnt, P9_TWALK, "ddT", oldfid->fid, fid->fid,
								nwname, wnames);
	if (IS_ERR(req)) {
		err = PTR_ERR(req);
		goto error;
	}

	err = p9pdu_readf(req->rc, clnt->proto_version, "R", &nwqids, &wqids);
	if (err) {
		p9pdu_dump(1, req->rc);
		p9_free_req(clnt, req);
		goto clunk_fid;
	}
	p9_free_req(clnt, req);

	P9_DPRINTK(P9_DEBUG_9P, "<<< RWALK nwqid %d:\n", nwqids);

	if (nwqids != nwname) {
		err = -ENOENT;
		goto clunk_fid;
	}

	for (count = 0; count < nwqids; count++)
		P9_DPRINTK(P9_DEBUG_9P, "<<<     [%d] %x.%llx.%x\n",
			count, wqids[count].type,
			(unsigned long long)wqids[count].path,
			wqids[count].version);

	if (nwname)
		memmove(&fid->qid, &wqids[nwqids - 1], sizeof(struct p9_qid));
	else
		fid->qid = oldfid->qid;

	return fid;

clunk_fid:
	p9_client_clunk(fid);
	fid = NULL;

error:
	if (fid && (fid != oldfid))
		p9_fid_destroy(fid);

	return ERR_PTR(err);
}
EXPORT_SYMBOL(p9_client_walk);

int p9_client_open(struct p9_fid *fid, int mode)
{
	int err;
	struct p9_client *clnt;
	struct p9_req_t *req;
	struct p9_qid qid;
	int iounit;

	P9_DPRINTK(P9_DEBUG_9P, ">>> TOPEN fid %d mode %d\n", fid->fid, mode);
	err = 0;
	clnt = fid->clnt;

	if (fid->mode != -1)
		return -EINVAL;

	req = p9_client_rpc(clnt, P9_TOPEN, "db", fid->fid, mode);
	if (IS_ERR(req)) {
		err = PTR_ERR(req);
		goto error;
	}

	err = p9pdu_readf(req->rc, clnt->proto_version, "Qd", &qid, &iounit);
	if (err) {
		p9pdu_dump(1, req->rc);
		goto free_and_error;
	}

	P9_DPRINTK(P9_DEBUG_9P, "<<< ROPEN qid %x.%llx.%x iounit %x\n",
				qid.type,
				(unsigned long long)qid.path,
				qid.version, iounit);

	fid->mode = mode;
	fid->iounit = iounit;

free_and_error:
	p9_free_req(clnt, req);
error:
	return err;
}
EXPORT_SYMBOL(p9_client_open);

int p9_client_fcreate(struct p9_fid *fid, char *name, u32 perm, int mode,
		     char *extension)
{
	int err;
	struct p9_client *clnt;
	struct p9_req_t *req;
	struct p9_qid qid;
	int iounit;

	P9_DPRINTK(P9_DEBUG_9P, ">>> TCREATE fid %d name %s perm %d mode %d\n",
						fid->fid, name, perm, mode);
	err = 0;
	clnt = fid->clnt;

	if (fid->mode != -1)
		return -EINVAL;

	req = p9_client_rpc(clnt, P9_TCREATE, "dsdb?s", fid->fid, name, perm,
				mode, extension);
	if (IS_ERR(req)) {
		err = PTR_ERR(req);
		goto error;
	}

	err = p9pdu_readf(req->rc, clnt->proto_version, "Qd", &qid, &iounit);
	if (err) {
		p9pdu_dump(1, req->rc);
		goto free_and_error;
	}

	P9_DPRINTK(P9_DEBUG_9P, "<<< RCREATE qid %x.%llx.%x iounit %x\n",
				qid.type,
				(unsigned long long)qid.path,
				qid.version, iounit);

	fid->mode = mode;
	fid->iounit = iounit;

free_and_error:
	p9_free_req(clnt, req);
error:
	return err;
}
EXPORT_SYMBOL(p9_client_fcreate);

int p9_client_clunk(struct p9_fid *fid)
{
	int err;
	struct p9_client *clnt;
	struct p9_req_t *req;

	P9_DPRINTK(P9_DEBUG_9P, ">>> TCLUNK fid %d\n", fid->fid);
	err = 0;
	clnt = fid->clnt;

	req = p9_client_rpc(clnt, P9_TCLUNK, "d", fid->fid);
	if (IS_ERR(req)) {
		err = PTR_ERR(req);
		goto error;
	}

	P9_DPRINTK(P9_DEBUG_9P, "<<< RCLUNK fid %d\n", fid->fid);

	p9_free_req(clnt, req);
	p9_fid_destroy(fid);

error:
	return err;
}
EXPORT_SYMBOL(p9_client_clunk);

int p9_client_remove(struct p9_fid *fid)
{
	int err;
	struct p9_client *clnt;
	struct p9_req_t *req;

	P9_DPRINTK(P9_DEBUG_9P, ">>> TREMOVE fid %d\n", fid->fid);
	err = 0;
	clnt = fid->clnt;

	req = p9_client_rpc(clnt, P9_TREMOVE, "d", fid->fid);
	if (IS_ERR(req)) {
		err = PTR_ERR(req);
		goto error;
	}

	P9_DPRINTK(P9_DEBUG_9P, "<<< RREMOVE fid %d\n", fid->fid);

	p9_free_req(clnt, req);
	p9_fid_destroy(fid);

error:
	return err;
}
EXPORT_SYMBOL(p9_client_remove);

int
p9_client_read(struct p9_fid *fid, char *data, char __user *udata, u64 offset,
								u32 count)
{
	int err, rsize, total;
	struct p9_client *clnt;
	struct p9_req_t *req;
	char *dataptr;

	P9_DPRINTK(P9_DEBUG_9P, ">>> TREAD fid %d offset %llu %d\n", fid->fid,
					(long long unsigned) offset, count);
	err = 0;
	clnt = fid->clnt;
	total = 0;

	rsize = fid->iounit;
	if (!rsize || rsize > clnt->msize-P9_IOHDRSZ)
		rsize = clnt->msize - P9_IOHDRSZ;

	if (count < rsize)
		rsize = count;

	req = p9_client_rpc(clnt, P9_TREAD, "dqd", fid->fid, offset, rsize);
	if (IS_ERR(req)) {
		err = PTR_ERR(req);
		goto error;
	}

	err = p9pdu_readf(req->rc, clnt->proto_version, "D", &count, &dataptr);
	if (err) {
		p9pdu_dump(1, req->rc);
		goto free_and_error;
	}

	P9_DPRINTK(P9_DEBUG_9P, "<<< RREAD count %d\n", count);

	if (data) {
		memmove(data, dataptr, count);
	}

	if (udata) {
		err = copy_to_user(udata, dataptr, count);
		if (err) {
			err = -EFAULT;
			goto free_and_error;
		}
	}

	p9_free_req(clnt, req);
	return count;

free_and_error:
	p9_free_req(clnt, req);
error:
	return err;
}
EXPORT_SYMBOL(p9_client_read);

int
p9_client_write(struct p9_fid *fid, char *data, const char __user *udata,
							u64 offset, u32 count)
{
	int err, rsize, total;
	struct p9_client *clnt;
	struct p9_req_t *req;

	P9_DPRINTK(P9_DEBUG_9P, ">>> TWRITE fid %d offset %llu count %d\n",
				fid->fid, (long long unsigned) offset, count);
	err = 0;
	clnt = fid->clnt;
	total = 0;

	rsize = fid->iounit;
	if (!rsize || rsize > clnt->msize-P9_IOHDRSZ)
		rsize = clnt->msize - P9_IOHDRSZ;

	if (count < rsize)
		rsize = count;
	if (data)
		req = p9_client_rpc(clnt, P9_TWRITE, "dqD", fid->fid, offset,
								rsize, data);
	else
		req = p9_client_rpc(clnt, P9_TWRITE, "dqU", fid->fid, offset,
								rsize, udata);
	if (IS_ERR(req)) {
		err = PTR_ERR(req);
		goto error;
	}

	err = p9pdu_readf(req->rc, clnt->proto_version, "d", &count);
	if (err) {
		p9pdu_dump(1, req->rc);
		goto free_and_error;
	}

	P9_DPRINTK(P9_DEBUG_9P, "<<< RWRITE count %d\n", count);

	p9_free_req(clnt, req);
	return count;

free_and_error:
	p9_free_req(clnt, req);
error:
	return err;
}
EXPORT_SYMBOL(p9_client_write);

struct p9_wstat *p9_client_stat(struct p9_fid *fid)
{
	int err;
	struct p9_client *clnt;
	struct p9_wstat *ret = kmalloc(sizeof(struct p9_wstat), GFP_KERNEL);
	struct p9_req_t *req;
	u16 ignored;

	P9_DPRINTK(P9_DEBUG_9P, ">>> TSTAT fid %d\n", fid->fid);

	if (!ret)
		return ERR_PTR(-ENOMEM);

	err = 0;
	clnt = fid->clnt;

	req = p9_client_rpc(clnt, P9_TSTAT, "d", fid->fid);
	if (IS_ERR(req)) {
		err = PTR_ERR(req);
		goto error;
	}

	err = p9pdu_readf(req->rc, clnt->proto_version, "wS", &ignored, ret);
	if (err) {
		p9pdu_dump(1, req->rc);
		p9_free_req(clnt, req);
		goto error;
	}

	P9_DPRINTK(P9_DEBUG_9P,
		"<<< RSTAT sz=%x type=%x dev=%x qid=%x.%llx.%x\n"
		"<<<    mode=%8.8x atime=%8.8x mtime=%8.8x length=%llx\n"
		"<<<    name=%s uid=%s gid=%s muid=%s extension=(%s)\n"
		"<<<    uid=%d gid=%d n_muid=%d\n",
		ret->size, ret->type, ret->dev, ret->qid.type,
		(unsigned long long)ret->qid.path, ret->qid.version, ret->mode,
		ret->atime, ret->mtime, (unsigned long long)ret->length,
		ret->name, ret->uid, ret->gid, ret->muid, ret->extension,
		ret->n_uid, ret->n_gid, ret->n_muid);

	p9_free_req(clnt, req);
	return ret;

error:
	kfree(ret);
	return ERR_PTR(err);
}
EXPORT_SYMBOL(p9_client_stat);

static int p9_client_statsize(struct p9_wstat *wst, int proto_version)
{
	int ret;

	/* NOTE: size shouldn't include its own length */
	/* size[2] type[2] dev[4] qid[13] */
	/* mode[4] atime[4] mtime[4] length[8]*/
	/* name[s] uid[s] gid[s] muid[s] */
	ret = 2+4+13+4+4+4+8+2+2+2+2;

	if (wst->name)
		ret += strlen(wst->name);
	if (wst->uid)
		ret += strlen(wst->uid);
	if (wst->gid)
		ret += strlen(wst->gid);
	if (wst->muid)
		ret += strlen(wst->muid);

	if (proto_version == p9_proto_2000u) {
		ret += 2+4+4+4;	/* extension[s] n_uid[4] n_gid[4] n_muid[4] */
		if (wst->extension)
			ret += strlen(wst->extension);
	}

	return ret;
}

int p9_client_wstat(struct p9_fid *fid, struct p9_wstat *wst)
{
	int err;
	struct p9_req_t *req;
	struct p9_client *clnt;

	err = 0;
	clnt = fid->clnt;
	wst->size = p9_client_statsize(wst, clnt->proto_version);
	P9_DPRINTK(P9_DEBUG_9P, ">>> TWSTAT fid %d\n", fid->fid);
	P9_DPRINTK(P9_DEBUG_9P,
		"     sz=%x type=%x dev=%x qid=%x.%llx.%x\n"
		"     mode=%8.8x atime=%8.8x mtime=%8.8x length=%llx\n"
		"     name=%s uid=%s gid=%s muid=%s extension=(%s)\n"
		"     uid=%d gid=%d n_muid=%d\n",
		wst->size, wst->type, wst->dev, wst->qid.type,
		(unsigned long long)wst->qid.path, wst->qid.version, wst->mode,
		wst->atime, wst->mtime, (unsigned long long)wst->length,
		wst->name, wst->uid, wst->gid, wst->muid, wst->extension,
		wst->n_uid, wst->n_gid, wst->n_muid);

	req = p9_client_rpc(clnt, P9_TWSTAT, "dwS", fid->fid, wst->size+2, wst);
	if (IS_ERR(req)) {
		err = PTR_ERR(req);
		goto error;
	}

	P9_DPRINTK(P9_DEBUG_9P, "<<< RWSTAT fid %d\n", fid->fid);

	p9_free_req(clnt, req);
error:
	return err;
}
EXPORT_SYMBOL(p9_client_wstat);
