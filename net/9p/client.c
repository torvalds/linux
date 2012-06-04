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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

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

#define CREATE_TRACE_POINTS
#include <trace/events/9p.h>

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
	return clnt->proto_version == p9_proto_2000L;
}
EXPORT_SYMBOL(p9_is_proto_dotl);

inline int p9_is_proto_dotu(struct p9_client *clnt)
{
	return clnt->proto_version == p9_proto_2000u;
}
EXPORT_SYMBOL(p9_is_proto_dotu);

/* Interpret mount option for protocol version */
static int get_protocol_version(char *s)
{
	int version = -EINVAL;

	if (!strcmp(s, "9p2000")) {
		version = p9_proto_legacy;
		p9_debug(P9_DEBUG_9P, "Protocol version: Legacy\n");
	} else if (!strcmp(s, "9p2000.u")) {
		version = p9_proto_2000u;
		p9_debug(P9_DEBUG_9P, "Protocol version: 9P2000.u\n");
	} else if (!strcmp(s, "9p2000.L")) {
		version = p9_proto_2000L;
		p9_debug(P9_DEBUG_9P, "Protocol version: 9P2000.L\n");
	} else
		pr_info("Unknown protocol version %s\n", s);

	return version;
}

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
	char *s;
	int ret = 0;

	clnt->proto_version = p9_proto_2000u;
	clnt->msize = 8192;

	if (!opts)
		return 0;

	tmp_options = kstrdup(opts, GFP_KERNEL);
	if (!tmp_options) {
		p9_debug(P9_DEBUG_ERROR,
			 "failed to allocate copy of option string\n");
		return -ENOMEM;
	}
	options = tmp_options;

	while ((p = strsep(&options, ",")) != NULL) {
		int token, r;
		if (!*p)
			continue;
		token = match_token(p, tokens, args);
		switch (token) {
		case Opt_msize:
			r = match_int(&args[0], &option);
			if (r < 0) {
				p9_debug(P9_DEBUG_ERROR,
					 "integer field, but no integer?\n");
				ret = r;
				continue;
			}
			clnt->msize = option;
			break;
		case Opt_trans:
			s = match_strdup(&args[0]);
			if (!s) {
				ret = -ENOMEM;
				p9_debug(P9_DEBUG_ERROR,
					 "problem allocating copy of trans arg\n");
				goto free_and_return;
			 }
			clnt->trans_mod = v9fs_get_trans_by_name(s);
			if (clnt->trans_mod == NULL) {
				pr_info("Could not find request transport: %s\n",
					s);
				ret = -EINVAL;
				kfree(s);
				goto free_and_return;
			}
			kfree(s);
			break;
		case Opt_legacy:
			clnt->proto_version = p9_proto_legacy;
			break;
		case Opt_version:
			s = match_strdup(&args[0]);
			if (!s) {
				ret = -ENOMEM;
				p9_debug(P9_DEBUG_ERROR,
					 "problem allocating copy of version arg\n");
				goto free_and_return;
			}
			ret = get_protocol_version(s);
			if (ret == -EINVAL) {
				kfree(s);
				goto free_and_return;
			}
			kfree(s);
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
 * request_slots as necessary to accommodate transaction
 * ids which did not previously have a slot.
 *
 * this code relies on the client spinlock to manage locks, its
 * possible we should switch to something else, but I'd rather
 * stick with something low-overhead for the common case.
 *
 */

static struct p9_req_t *
p9_tag_alloc(struct p9_client *c, u16 tag, unsigned int max_size)
{
	unsigned long flags;
	int row, col;
	struct p9_req_t *req;
	int alloc_msize = min(c->msize, max_size);

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
				pr_err("Couldn't grow tag array\n");
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
		req->wq = kmalloc(sizeof(wait_queue_head_t), GFP_NOFS);
		if (!req->wq) {
			pr_err("Couldn't grow tag array\n");
			return ERR_PTR(-ENOMEM);
		}
		init_waitqueue_head(req->wq);
		req->tc = kmalloc(sizeof(struct p9_fcall) + alloc_msize,
				  GFP_NOFS);
		req->rc = kmalloc(sizeof(struct p9_fcall) + alloc_msize,
				  GFP_NOFS);
		if ((!req->tc) || (!req->rc)) {
			pr_err("Couldn't grow tag array\n");
			kfree(req->tc);
			kfree(req->rc);
			kfree(req->wq);
			req->tc = req->rc = NULL;
			req->wq = NULL;
			return ERR_PTR(-ENOMEM);
		}
		req->tc->capacity = alloc_msize;
		req->rc->capacity = alloc_msize;
		req->tc->sdata = (char *) req->tc + sizeof(struct p9_fcall);
		req->rc->sdata = (char *) req->rc + sizeof(struct p9_fcall);
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

	if(tag >= c->max_tag) 
		return NULL;

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
		goto error;
	}
	err = p9_idpool_get(c->tagpool); /* reserve tag 0 */
	if (err < 0) {
		p9_idpool_destroy(c->tagpool);
		goto error;
	}
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
				p9_debug(P9_DEBUG_MUX,
					 "Attempting to cleanup non-free tag %d,%d\n",
					 row, col);
				/* TODO: delay execution of cleanup */
				return;
			}
		}
	}

	if (c->tagpool) {
		p9_idpool_put(0, c->tagpool); /* free reserved tag 0 */
		p9_idpool_destroy(c->tagpool);
	}

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
	p9_debug(P9_DEBUG_MUX, "clnt %p req %p tag: %d\n", c, r, tag);

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
	p9_debug(P9_DEBUG_MUX, " tag %d\n", req->tc->tag);
	wake_up(req->wq);
	p9_debug(P9_DEBUG_MUX, "wakeup: %d\n", req->tc->tag);
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

	p9_debug(P9_DEBUG_9P, "<<< size=%d type: %d tag: %d\n",
		 pdu->size, pdu->id, pdu->tag);

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
	int ecode;

	err = p9_parse_header(req->rc, NULL, &type, NULL, 0);
	/*
	 * dump the response from server
	 * This should be after check errors which poplulate pdu_fcall.
	 */
	trace_9p_protocol_dump(c, req->rc);
	if (err) {
		p9_debug(P9_DEBUG_ERROR, "couldn't parse header %d\n", err);
		return err;
	}
	if (type != P9_RERROR && type != P9_RLERROR)
		return 0;

	if (!p9_is_proto_dotl(c)) {
		char *ename;
		err = p9pdu_readf(req->rc, c->proto_version, "s?d",
				  &ename, &ecode);
		if (err)
			goto out_err;

		if (p9_is_proto_dotu(c))
			err = -ecode;

		if (!err || !IS_ERR_VALUE(err)) {
			err = p9_errstr2errno(ename, strlen(ename));

			p9_debug(P9_DEBUG_9P, "<<< RERROR (%d) %s\n",
				 -ecode, ename);
		}
		kfree(ename);
	} else {
		err = p9pdu_readf(req->rc, c->proto_version, "d", &ecode);
		err = -ecode;

		p9_debug(P9_DEBUG_9P, "<<< RLERROR (%d)\n", -ecode);
	}

	return err;

out_err:
	p9_debug(P9_DEBUG_ERROR, "couldn't parse error%d\n", err);

	return err;
}

/**
 * p9_check_zc_errors - check 9p packet for error return and process it
 * @c: current client instance
 * @req: request to parse and check for error conditions
 * @in_hdrlen: Size of response protocol buffer.
 *
 * returns error code if one is discovered, otherwise returns 0
 *
 * this will have to be more complicated if we have multiple
 * error packet types
 */

static int p9_check_zc_errors(struct p9_client *c, struct p9_req_t *req,
			      char *uidata, int in_hdrlen, int kern_buf)
{
	int err;
	int ecode;
	int8_t type;
	char *ename = NULL;

	err = p9_parse_header(req->rc, NULL, &type, NULL, 0);
	/*
	 * dump the response from server
	 * This should be after parse_header which poplulate pdu_fcall.
	 */
	trace_9p_protocol_dump(c, req->rc);
	if (err) {
		p9_debug(P9_DEBUG_ERROR, "couldn't parse header %d\n", err);
		return err;
	}

	if (type != P9_RERROR && type != P9_RLERROR)
		return 0;

	if (!p9_is_proto_dotl(c)) {
		/* Error is reported in string format */
		uint16_t len;
		/* 7 = header size for RERROR, 2 is the size of string len; */
		int inline_len = in_hdrlen - (7 + 2);

		/* Read the size of error string */
		err = p9pdu_readf(req->rc, c->proto_version, "w", &len);
		if (err)
			goto out_err;

		ename = kmalloc(len + 1, GFP_NOFS);
		if (!ename) {
			err = -ENOMEM;
			goto out_err;
		}
		if (len <= inline_len) {
			/* We have error in protocol buffer itself */
			if (pdu_read(req->rc, ename, len)) {
				err = -EFAULT;
				goto out_free;

			}
		} else {
			/*
			 *  Part of the data is in user space buffer.
			 */
			if (pdu_read(req->rc, ename, inline_len)) {
				err = -EFAULT;
				goto out_free;

			}
			if (kern_buf) {
				memcpy(ename + inline_len, uidata,
				       len - inline_len);
			} else {
				err = copy_from_user(ename + inline_len,
						     uidata, len - inline_len);
				if (err) {
					err = -EFAULT;
					goto out_free;
				}
			}
		}
		ename[len] = 0;
		if (p9_is_proto_dotu(c)) {
			/* For dotu we also have error code */
			err = p9pdu_readf(req->rc,
					  c->proto_version, "d", &ecode);
			if (err)
				goto out_free;
			err = -ecode;
		}
		if (!err || !IS_ERR_VALUE(err)) {
			err = p9_errstr2errno(ename, strlen(ename));

			p9_debug(P9_DEBUG_9P, "<<< RERROR (%d) %s\n",
				 -ecode, ename);
		}
		kfree(ename);
	} else {
		err = p9pdu_readf(req->rc, c->proto_version, "d", &ecode);
		err = -ecode;

		p9_debug(P9_DEBUG_9P, "<<< RLERROR (%d)\n", -ecode);
	}
	return err;

out_free:
	kfree(ename);
out_err:
	p9_debug(P9_DEBUG_ERROR, "couldn't parse error%d\n", err);
	return err;
}

static struct p9_req_t *
p9_client_rpc(struct p9_client *c, int8_t type, const char *fmt, ...);

/**
 * p9_client_flush - flush (cancel) a request
 * @c: client state
 * @oldreq: request to cancel
 *
 * This sents a flush for a particular request and links
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

	p9_debug(P9_DEBUG_9P, ">>> TFLUSH tag %d\n", oldtag);

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

static struct p9_req_t *p9_client_prepare_req(struct p9_client *c,
					      int8_t type, int req_size,
					      const char *fmt, va_list ap)
{
	int tag, err;
	struct p9_req_t *req;

	p9_debug(P9_DEBUG_MUX, "client %p op %d\n", c, type);

	/* we allow for any status other than disconnected */
	if (c->status == Disconnected)
		return ERR_PTR(-EIO);

	/* if status is begin_disconnected we allow only clunk request */
	if ((c->status == BeginDisconnect) && (type != P9_TCLUNK))
		return ERR_PTR(-EIO);

	tag = P9_NOTAG;
	if (type != P9_TVERSION) {
		tag = p9_idpool_get(c->tagpool);
		if (tag < 0)
			return ERR_PTR(-ENOMEM);
	}

	req = p9_tag_alloc(c, tag, req_size);
	if (IS_ERR(req))
		return req;

	/* marshall the data */
	p9pdu_prepare(req->tc, tag, type);
	err = p9pdu_vwritef(req->tc, c->proto_version, fmt, ap);
	if (err)
		goto reterr;
	p9pdu_finalize(c, req->tc);
	trace_9p_client_req(c, type, tag);
	return req;
reterr:
	p9_free_req(c, req);
	return ERR_PTR(err);
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
	int sigpending, err;
	unsigned long flags;
	struct p9_req_t *req;

	va_start(ap, fmt);
	req = p9_client_prepare_req(c, type, c->msize, fmt, ap);
	va_end(ap);
	if (IS_ERR(req))
		return req;

	if (signal_pending(current)) {
		sigpending = 1;
		clear_thread_flag(TIF_SIGPENDING);
	} else
		sigpending = 0;

	err = c->trans_mod->request(c, req);
	if (err < 0) {
		if (err != -ERESTARTSYS && err != -EFAULT)
			c->status = Disconnected;
		goto reterr;
	}
again:
	/* Wait for the response */
	err = wait_event_interruptible(*req->wq,
				       req->status >= REQ_STATUS_RCVD);

	if ((err == -ERESTARTSYS) && (c->status == Connected)
				  && (type == P9_TFLUSH)) {
		sigpending = 1;
		clear_thread_flag(TIF_SIGPENDING);
		goto again;
	}

	if (req->status == REQ_STATUS_ERROR) {
		p9_debug(P9_DEBUG_ERROR, "req_status error %d\n", req->t_err);
		err = req->t_err;
	}
	if ((err == -ERESTARTSYS) && (c->status == Connected)) {
		p9_debug(P9_DEBUG_MUX, "flushing\n");
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
	trace_9p_client_res(c, type, req->rc->tag, err);
	if (!err)
		return req;
reterr:
	p9_free_req(c, req);
	return ERR_PTR(err);
}

/**
 * p9_client_zc_rpc - issue a request and wait for a response
 * @c: client session
 * @type: type of request
 * @uidata: user bffer that should be ued for zero copy read
 * @uodata: user buffer that shoud be user for zero copy write
 * @inlen: read buffer size
 * @olen: write buffer size
 * @hdrlen: reader header size, This is the size of response protocol data
 * @fmt: protocol format string (see protocol.c)
 *
 * Returns request structure (which client must free using p9_free_req)
 */
static struct p9_req_t *p9_client_zc_rpc(struct p9_client *c, int8_t type,
					 char *uidata, char *uodata,
					 int inlen, int olen, int in_hdrlen,
					 int kern_buf, const char *fmt, ...)
{
	va_list ap;
	int sigpending, err;
	unsigned long flags;
	struct p9_req_t *req;

	va_start(ap, fmt);
	/*
	 * We allocate a inline protocol data of only 4k bytes.
	 * The actual content is passed in zero-copy fashion.
	 */
	req = p9_client_prepare_req(c, type, P9_ZC_HDR_SZ, fmt, ap);
	va_end(ap);
	if (IS_ERR(req))
		return req;

	if (signal_pending(current)) {
		sigpending = 1;
		clear_thread_flag(TIF_SIGPENDING);
	} else
		sigpending = 0;

	/* If we are called with KERNEL_DS force kern_buf */
	if (segment_eq(get_fs(), KERNEL_DS))
		kern_buf = 1;

	err = c->trans_mod->zc_request(c, req, uidata, uodata,
				       inlen, olen, in_hdrlen, kern_buf);
	if (err < 0) {
		if (err == -EIO)
			c->status = Disconnected;
		goto reterr;
	}
	if (req->status == REQ_STATUS_ERROR) {
		p9_debug(P9_DEBUG_ERROR, "req_status error %d\n", req->t_err);
		err = req->t_err;
	}
	if ((err == -ERESTARTSYS) && (c->status == Connected)) {
		p9_debug(P9_DEBUG_MUX, "flushing\n");
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

	err = p9_check_zc_errors(c, req, uidata, in_hdrlen, kern_buf);
	trace_9p_client_res(c, type, req->rc->tag, err);
	if (!err)
		return req;
reterr:
	p9_free_req(c, req);
	return ERR_PTR(err);
}

static struct p9_fid *p9_fid_create(struct p9_client *clnt)
{
	int ret;
	struct p9_fid *fid;
	unsigned long flags;

	p9_debug(P9_DEBUG_FID, "clnt %p\n", clnt);
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

	p9_debug(P9_DEBUG_FID, "fid %d\n", fid->fid);
	clnt = fid->clnt;
	p9_idpool_put(fid->fid, clnt->fidpool);
	spin_lock_irqsave(&clnt->lock, flags);
	list_del(&fid->flist);
	spin_unlock_irqrestore(&clnt->lock, flags);
	kfree(fid->rdir);
	kfree(fid);
}

static int p9_client_version(struct p9_client *c)
{
	int err = 0;
	struct p9_req_t *req;
	char *version;
	int msize;

	p9_debug(P9_DEBUG_9P, ">>> TVERSION msize %d protocol %d\n",
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
		p9_debug(P9_DEBUG_9P, "version error %d\n", err);
		trace_9p_protocol_dump(c, req->rc);
		goto error;
	}

	p9_debug(P9_DEBUG_9P, "<<< RVERSION msize %d %s\n", msize, version);
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

	err = p9_tag_init(clnt);
	if (err < 0)
		goto free_client;

	err = parse_opts(options, clnt);
	if (err < 0)
		goto destroy_tagpool;

	if (!clnt->trans_mod)
		clnt->trans_mod = v9fs_get_default_trans();

	if (clnt->trans_mod == NULL) {
		err = -EPROTONOSUPPORT;
		p9_debug(P9_DEBUG_ERROR,
			 "No transport defined or default transport\n");
		goto destroy_tagpool;
	}

	clnt->fidpool = p9_idpool_create();
	if (IS_ERR(clnt->fidpool)) {
		err = PTR_ERR(clnt->fidpool);
		goto put_trans;
	}

	p9_debug(P9_DEBUG_MUX, "clnt %p trans %p msize %d protocol %d\n",
		 clnt, clnt->trans_mod, clnt->msize, clnt->proto_version);

	err = clnt->trans_mod->create(clnt, dev_name, options);
	if (err)
		goto destroy_fidpool;

	if (clnt->msize > clnt->trans_mod->maxsize)
		clnt->msize = clnt->trans_mod->maxsize;

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
destroy_tagpool:
	p9_idpool_destroy(clnt->tagpool);
free_client:
	kfree(clnt);
	return ERR_PTR(err);
}
EXPORT_SYMBOL(p9_client_create);

void p9_client_destroy(struct p9_client *clnt)
{
	struct p9_fid *fid, *fidptr;

	p9_debug(P9_DEBUG_MUX, "clnt %p\n", clnt);

	if (clnt->trans_mod)
		clnt->trans_mod->close(clnt);

	v9fs_put_trans(clnt->trans_mod);

	list_for_each_entry_safe(fid, fidptr, &clnt->fidlist, flist) {
		pr_info("Found fid %d not clunked\n", fid->fid);
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
	p9_debug(P9_DEBUG_9P, "clnt %p\n", clnt);
	clnt->status = Disconnected;
}
EXPORT_SYMBOL(p9_client_disconnect);

void p9_client_begin_disconnect(struct p9_client *clnt)
{
	p9_debug(P9_DEBUG_9P, "clnt %p\n", clnt);
	clnt->status = BeginDisconnect;
}
EXPORT_SYMBOL(p9_client_begin_disconnect);

struct p9_fid *p9_client_attach(struct p9_client *clnt, struct p9_fid *afid,
	char *uname, u32 n_uname, char *aname)
{
	int err = 0;
	struct p9_req_t *req;
	struct p9_fid *fid;
	struct p9_qid qid;


	p9_debug(P9_DEBUG_9P, ">>> TATTACH afid %d uname %s aname %s\n",
		 afid ? afid->fid : -1, uname, aname);
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
		trace_9p_protocol_dump(clnt, req->rc);
		p9_free_req(clnt, req);
		goto error;
	}

	p9_debug(P9_DEBUG_9P, "<<< RATTACH qid %x.%llx.%x\n",
		 qid.type, (unsigned long long)qid.path, qid.version);

	memmove(&fid->qid, &qid, sizeof(struct p9_qid));

	p9_free_req(clnt, req);
	return fid;

error:
	if (fid)
		p9_fid_destroy(fid);
	return ERR_PTR(err);
}
EXPORT_SYMBOL(p9_client_attach);

struct p9_fid *p9_client_walk(struct p9_fid *oldfid, uint16_t nwname,
		char **wnames, int clone)
{
	int err;
	struct p9_client *clnt;
	struct p9_fid *fid;
	struct p9_qid *wqids;
	struct p9_req_t *req;
	uint16_t nwqids, count;

	err = 0;
	wqids = NULL;
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


	p9_debug(P9_DEBUG_9P, ">>> TWALK fids %d,%d nwname %ud wname[0] %s\n",
		 oldfid->fid, fid->fid, nwname, wnames ? wnames[0] : NULL);

	req = p9_client_rpc(clnt, P9_TWALK, "ddT", oldfid->fid, fid->fid,
								nwname, wnames);
	if (IS_ERR(req)) {
		err = PTR_ERR(req);
		goto error;
	}

	err = p9pdu_readf(req->rc, clnt->proto_version, "R", &nwqids, &wqids);
	if (err) {
		trace_9p_protocol_dump(clnt, req->rc);
		p9_free_req(clnt, req);
		goto clunk_fid;
	}
	p9_free_req(clnt, req);

	p9_debug(P9_DEBUG_9P, "<<< RWALK nwqid %d:\n", nwqids);

	if (nwqids != nwname) {
		err = -ENOENT;
		goto clunk_fid;
	}

	for (count = 0; count < nwqids; count++)
		p9_debug(P9_DEBUG_9P, "<<<     [%d] %x.%llx.%x\n",
			count, wqids[count].type,
			(unsigned long long)wqids[count].path,
			wqids[count].version);

	if (nwname)
		memmove(&fid->qid, &wqids[nwqids - 1], sizeof(struct p9_qid));
	else
		fid->qid = oldfid->qid;

	kfree(wqids);
	return fid;

clunk_fid:
	kfree(wqids);
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

	clnt = fid->clnt;
	p9_debug(P9_DEBUG_9P, ">>> %s fid %d mode %d\n",
		p9_is_proto_dotl(clnt) ? "TLOPEN" : "TOPEN", fid->fid, mode);
	err = 0;

	if (fid->mode != -1)
		return -EINVAL;

	if (p9_is_proto_dotl(clnt))
		req = p9_client_rpc(clnt, P9_TLOPEN, "dd", fid->fid, mode);
	else
		req = p9_client_rpc(clnt, P9_TOPEN, "db", fid->fid, mode);
	if (IS_ERR(req)) {
		err = PTR_ERR(req);
		goto error;
	}

	err = p9pdu_readf(req->rc, clnt->proto_version, "Qd", &qid, &iounit);
	if (err) {
		trace_9p_protocol_dump(clnt, req->rc);
		goto free_and_error;
	}

	p9_debug(P9_DEBUG_9P, "<<< %s qid %x.%llx.%x iounit %x\n",
		p9_is_proto_dotl(clnt) ? "RLOPEN" : "ROPEN",  qid.type,
		(unsigned long long)qid.path, qid.version, iounit);

	fid->mode = mode;
	fid->iounit = iounit;

free_and_error:
	p9_free_req(clnt, req);
error:
	return err;
}
EXPORT_SYMBOL(p9_client_open);

int p9_client_create_dotl(struct p9_fid *ofid, char *name, u32 flags, u32 mode,
		gid_t gid, struct p9_qid *qid)
{
	int err = 0;
	struct p9_client *clnt;
	struct p9_req_t *req;
	int iounit;

	p9_debug(P9_DEBUG_9P,
			">>> TLCREATE fid %d name %s flags %d mode %d gid %d\n",
			ofid->fid, name, flags, mode, gid);
	clnt = ofid->clnt;

	if (ofid->mode != -1)
		return -EINVAL;

	req = p9_client_rpc(clnt, P9_TLCREATE, "dsddd", ofid->fid, name, flags,
			mode, gid);
	if (IS_ERR(req)) {
		err = PTR_ERR(req);
		goto error;
	}

	err = p9pdu_readf(req->rc, clnt->proto_version, "Qd", qid, &iounit);
	if (err) {
		trace_9p_protocol_dump(clnt, req->rc);
		goto free_and_error;
	}

	p9_debug(P9_DEBUG_9P, "<<< RLCREATE qid %x.%llx.%x iounit %x\n",
			qid->type,
			(unsigned long long)qid->path,
			qid->version, iounit);

	ofid->mode = mode;
	ofid->iounit = iounit;

free_and_error:
	p9_free_req(clnt, req);
error:
	return err;
}
EXPORT_SYMBOL(p9_client_create_dotl);

int p9_client_fcreate(struct p9_fid *fid, char *name, u32 perm, int mode,
		     char *extension)
{
	int err;
	struct p9_client *clnt;
	struct p9_req_t *req;
	struct p9_qid qid;
	int iounit;

	p9_debug(P9_DEBUG_9P, ">>> TCREATE fid %d name %s perm %d mode %d\n",
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
		trace_9p_protocol_dump(clnt, req->rc);
		goto free_and_error;
	}

	p9_debug(P9_DEBUG_9P, "<<< RCREATE qid %x.%llx.%x iounit %x\n",
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

int p9_client_symlink(struct p9_fid *dfid, char *name, char *symtgt, gid_t gid,
		struct p9_qid *qid)
{
	int err = 0;
	struct p9_client *clnt;
	struct p9_req_t *req;

	p9_debug(P9_DEBUG_9P, ">>> TSYMLINK dfid %d name %s  symtgt %s\n",
			dfid->fid, name, symtgt);
	clnt = dfid->clnt;

	req = p9_client_rpc(clnt, P9_TSYMLINK, "dssd", dfid->fid, name, symtgt,
			gid);
	if (IS_ERR(req)) {
		err = PTR_ERR(req);
		goto error;
	}

	err = p9pdu_readf(req->rc, clnt->proto_version, "Q", qid);
	if (err) {
		trace_9p_protocol_dump(clnt, req->rc);
		goto free_and_error;
	}

	p9_debug(P9_DEBUG_9P, "<<< RSYMLINK qid %x.%llx.%x\n",
			qid->type, (unsigned long long)qid->path, qid->version);

free_and_error:
	p9_free_req(clnt, req);
error:
	return err;
}
EXPORT_SYMBOL(p9_client_symlink);

int p9_client_link(struct p9_fid *dfid, struct p9_fid *oldfid, char *newname)
{
	struct p9_client *clnt;
	struct p9_req_t *req;

	p9_debug(P9_DEBUG_9P, ">>> TLINK dfid %d oldfid %d newname %s\n",
			dfid->fid, oldfid->fid, newname);
	clnt = dfid->clnt;
	req = p9_client_rpc(clnt, P9_TLINK, "dds", dfid->fid, oldfid->fid,
			newname);
	if (IS_ERR(req))
		return PTR_ERR(req);

	p9_debug(P9_DEBUG_9P, "<<< RLINK\n");
	p9_free_req(clnt, req);
	return 0;
}
EXPORT_SYMBOL(p9_client_link);

int p9_client_fsync(struct p9_fid *fid, int datasync)
{
	int err;
	struct p9_client *clnt;
	struct p9_req_t *req;

	p9_debug(P9_DEBUG_9P, ">>> TFSYNC fid %d datasync:%d\n",
			fid->fid, datasync);
	err = 0;
	clnt = fid->clnt;

	req = p9_client_rpc(clnt, P9_TFSYNC, "dd", fid->fid, datasync);
	if (IS_ERR(req)) {
		err = PTR_ERR(req);
		goto error;
	}

	p9_debug(P9_DEBUG_9P, "<<< RFSYNC fid %d\n", fid->fid);

	p9_free_req(clnt, req);

error:
	return err;
}
EXPORT_SYMBOL(p9_client_fsync);

int p9_client_clunk(struct p9_fid *fid)
{
	int err;
	struct p9_client *clnt;
	struct p9_req_t *req;
	int retries = 0;

	if (!fid) {
		pr_warn("%s (%d): Trying to clunk with NULL fid\n",
			__func__, task_pid_nr(current));
		dump_stack();
		return 0;
	}

again:
	p9_debug(P9_DEBUG_9P, ">>> TCLUNK fid %d (try %d)\n", fid->fid,
								retries);
	err = 0;
	clnt = fid->clnt;

	req = p9_client_rpc(clnt, P9_TCLUNK, "d", fid->fid);
	if (IS_ERR(req)) {
		err = PTR_ERR(req);
		goto error;
	}

	p9_debug(P9_DEBUG_9P, "<<< RCLUNK fid %d\n", fid->fid);

	p9_free_req(clnt, req);
error:
	/*
	 * Fid is not valid even after a failed clunk
	 * If interrupted, retry once then give up and
	 * leak fid until umount.
	 */
	if (err == -ERESTARTSYS) {
		if (retries++ == 0)
			goto again;
	} else
		p9_fid_destroy(fid);
	return err;
}
EXPORT_SYMBOL(p9_client_clunk);

int p9_client_remove(struct p9_fid *fid)
{
	int err;
	struct p9_client *clnt;
	struct p9_req_t *req;

	p9_debug(P9_DEBUG_9P, ">>> TREMOVE fid %d\n", fid->fid);
	err = 0;
	clnt = fid->clnt;

	req = p9_client_rpc(clnt, P9_TREMOVE, "d", fid->fid);
	if (IS_ERR(req)) {
		err = PTR_ERR(req);
		goto error;
	}

	p9_debug(P9_DEBUG_9P, "<<< RREMOVE fid %d\n", fid->fid);

	p9_free_req(clnt, req);
error:
	if (err == -ERESTARTSYS)
		p9_client_clunk(fid);
	else
		p9_fid_destroy(fid);
	return err;
}
EXPORT_SYMBOL(p9_client_remove);

int p9_client_unlinkat(struct p9_fid *dfid, const char *name, int flags)
{
	int err = 0;
	struct p9_req_t *req;
	struct p9_client *clnt;

	p9_debug(P9_DEBUG_9P, ">>> TUNLINKAT fid %d %s %d\n",
		   dfid->fid, name, flags);

	clnt = dfid->clnt;
	req = p9_client_rpc(clnt, P9_TUNLINKAT, "dsd", dfid->fid, name, flags);
	if (IS_ERR(req)) {
		err = PTR_ERR(req);
		goto error;
	}
	p9_debug(P9_DEBUG_9P, "<<< RUNLINKAT fid %d %s\n", dfid->fid, name);

	p9_free_req(clnt, req);
error:
	return err;
}
EXPORT_SYMBOL(p9_client_unlinkat);

int
p9_client_read(struct p9_fid *fid, char *data, char __user *udata, u64 offset,
								u32 count)
{
	char *dataptr;
	int kernel_buf = 0;
	struct p9_req_t *req;
	struct p9_client *clnt;
	int err, rsize, non_zc = 0;


	p9_debug(P9_DEBUG_9P, ">>> TREAD fid %d offset %llu %d\n",
		   fid->fid, (unsigned long long) offset, count);
	err = 0;
	clnt = fid->clnt;

	rsize = fid->iounit;
	if (!rsize || rsize > clnt->msize-P9_IOHDRSZ)
		rsize = clnt->msize - P9_IOHDRSZ;

	if (count < rsize)
		rsize = count;

	/* Don't bother zerocopy for small IO (< 1024) */
	if (clnt->trans_mod->zc_request && rsize > 1024) {
		char *indata;
		if (data) {
			kernel_buf = 1;
			indata = data;
		} else
			indata = (__force char *)udata;
		/*
		 * response header len is 11
		 * PDU Header(7) + IO Size (4)
		 */
		req = p9_client_zc_rpc(clnt, P9_TREAD, indata, NULL, rsize, 0,
				       11, kernel_buf, "dqd", fid->fid,
				       offset, rsize);
	} else {
		non_zc = 1;
		req = p9_client_rpc(clnt, P9_TREAD, "dqd", fid->fid, offset,
				    rsize);
	}
	if (IS_ERR(req)) {
		err = PTR_ERR(req);
		goto error;
	}

	err = p9pdu_readf(req->rc, clnt->proto_version, "D", &count, &dataptr);
	if (err) {
		trace_9p_protocol_dump(clnt, req->rc);
		goto free_and_error;
	}

	p9_debug(P9_DEBUG_9P, "<<< RREAD count %d\n", count);

	if (non_zc) {
		if (data) {
			memmove(data, dataptr, count);
		} else {
			err = copy_to_user(udata, dataptr, count);
			if (err) {
				err = -EFAULT;
				goto free_and_error;
			}
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
	int err, rsize;
	int kernel_buf = 0;
	struct p9_client *clnt;
	struct p9_req_t *req;

	p9_debug(P9_DEBUG_9P, ">>> TWRITE fid %d offset %llu count %d\n",
				fid->fid, (unsigned long long) offset, count);
	err = 0;
	clnt = fid->clnt;

	rsize = fid->iounit;
	if (!rsize || rsize > clnt->msize-P9_IOHDRSZ)
		rsize = clnt->msize - P9_IOHDRSZ;

	if (count < rsize)
		rsize = count;

	/* Don't bother zerocopy for small IO (< 1024) */
	if (clnt->trans_mod->zc_request && rsize > 1024) {
		char *odata;
		if (data) {
			kernel_buf = 1;
			odata = data;
		} else
			odata = (char *)udata;
		req = p9_client_zc_rpc(clnt, P9_TWRITE, NULL, odata, 0, rsize,
				       P9_ZC_HDR_SZ, kernel_buf, "dqd",
				       fid->fid, offset, rsize);
	} else {
		if (data)
			req = p9_client_rpc(clnt, P9_TWRITE, "dqD", fid->fid,
					    offset, rsize, data);
		else
			req = p9_client_rpc(clnt, P9_TWRITE, "dqU", fid->fid,
					    offset, rsize, udata);
	}
	if (IS_ERR(req)) {
		err = PTR_ERR(req);
		goto error;
	}

	err = p9pdu_readf(req->rc, clnt->proto_version, "d", &count);
	if (err) {
		trace_9p_protocol_dump(clnt, req->rc);
		goto free_and_error;
	}

	p9_debug(P9_DEBUG_9P, "<<< RWRITE count %d\n", count);

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

	p9_debug(P9_DEBUG_9P, ">>> TSTAT fid %d\n", fid->fid);

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
		trace_9p_protocol_dump(clnt, req->rc);
		p9_free_req(clnt, req);
		goto error;
	}

	p9_debug(P9_DEBUG_9P,
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

struct p9_stat_dotl *p9_client_getattr_dotl(struct p9_fid *fid,
							u64 request_mask)
{
	int err;
	struct p9_client *clnt;
	struct p9_stat_dotl *ret = kmalloc(sizeof(struct p9_stat_dotl),
								GFP_KERNEL);
	struct p9_req_t *req;

	p9_debug(P9_DEBUG_9P, ">>> TGETATTR fid %d, request_mask %lld\n",
							fid->fid, request_mask);

	if (!ret)
		return ERR_PTR(-ENOMEM);

	err = 0;
	clnt = fid->clnt;

	req = p9_client_rpc(clnt, P9_TGETATTR, "dq", fid->fid, request_mask);
	if (IS_ERR(req)) {
		err = PTR_ERR(req);
		goto error;
	}

	err = p9pdu_readf(req->rc, clnt->proto_version, "A", ret);
	if (err) {
		trace_9p_protocol_dump(clnt, req->rc);
		p9_free_req(clnt, req);
		goto error;
	}

	p9_debug(P9_DEBUG_9P,
		"<<< RGETATTR st_result_mask=%lld\n"
		"<<< qid=%x.%llx.%x\n"
		"<<< st_mode=%8.8x st_nlink=%llu\n"
		"<<< st_uid=%d st_gid=%d\n"
		"<<< st_rdev=%llx st_size=%llx st_blksize=%llu st_blocks=%llu\n"
		"<<< st_atime_sec=%lld st_atime_nsec=%lld\n"
		"<<< st_mtime_sec=%lld st_mtime_nsec=%lld\n"
		"<<< st_ctime_sec=%lld st_ctime_nsec=%lld\n"
		"<<< st_btime_sec=%lld st_btime_nsec=%lld\n"
		"<<< st_gen=%lld st_data_version=%lld",
		ret->st_result_mask, ret->qid.type, ret->qid.path,
		ret->qid.version, ret->st_mode, ret->st_nlink, ret->st_uid,
		ret->st_gid, ret->st_rdev, ret->st_size, ret->st_blksize,
		ret->st_blocks, ret->st_atime_sec, ret->st_atime_nsec,
		ret->st_mtime_sec, ret->st_mtime_nsec, ret->st_ctime_sec,
		ret->st_ctime_nsec, ret->st_btime_sec, ret->st_btime_nsec,
		ret->st_gen, ret->st_data_version);

	p9_free_req(clnt, req);
	return ret;

error:
	kfree(ret);
	return ERR_PTR(err);
}
EXPORT_SYMBOL(p9_client_getattr_dotl);

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

	if ((proto_version == p9_proto_2000u) ||
		(proto_version == p9_proto_2000L)) {
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
	p9_debug(P9_DEBUG_9P, ">>> TWSTAT fid %d\n", fid->fid);
	p9_debug(P9_DEBUG_9P,
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

	p9_debug(P9_DEBUG_9P, "<<< RWSTAT fid %d\n", fid->fid);

	p9_free_req(clnt, req);
error:
	return err;
}
EXPORT_SYMBOL(p9_client_wstat);

int p9_client_setattr(struct p9_fid *fid, struct p9_iattr_dotl *p9attr)
{
	int err;
	struct p9_req_t *req;
	struct p9_client *clnt;

	err = 0;
	clnt = fid->clnt;
	p9_debug(P9_DEBUG_9P, ">>> TSETATTR fid %d\n", fid->fid);
	p9_debug(P9_DEBUG_9P,
		"    valid=%x mode=%x uid=%d gid=%d size=%lld\n"
		"    atime_sec=%lld atime_nsec=%lld\n"
		"    mtime_sec=%lld mtime_nsec=%lld\n",
		p9attr->valid, p9attr->mode, p9attr->uid, p9attr->gid,
		p9attr->size, p9attr->atime_sec, p9attr->atime_nsec,
		p9attr->mtime_sec, p9attr->mtime_nsec);

	req = p9_client_rpc(clnt, P9_TSETATTR, "dI", fid->fid, p9attr);

	if (IS_ERR(req)) {
		err = PTR_ERR(req);
		goto error;
	}
	p9_debug(P9_DEBUG_9P, "<<< RSETATTR fid %d\n", fid->fid);
	p9_free_req(clnt, req);
error:
	return err;
}
EXPORT_SYMBOL(p9_client_setattr);

int p9_client_statfs(struct p9_fid *fid, struct p9_rstatfs *sb)
{
	int err;
	struct p9_req_t *req;
	struct p9_client *clnt;

	err = 0;
	clnt = fid->clnt;

	p9_debug(P9_DEBUG_9P, ">>> TSTATFS fid %d\n", fid->fid);

	req = p9_client_rpc(clnt, P9_TSTATFS, "d", fid->fid);
	if (IS_ERR(req)) {
		err = PTR_ERR(req);
		goto error;
	}

	err = p9pdu_readf(req->rc, clnt->proto_version, "ddqqqqqqd", &sb->type,
		&sb->bsize, &sb->blocks, &sb->bfree, &sb->bavail,
		&sb->files, &sb->ffree, &sb->fsid, &sb->namelen);
	if (err) {
		trace_9p_protocol_dump(clnt, req->rc);
		p9_free_req(clnt, req);
		goto error;
	}

	p9_debug(P9_DEBUG_9P, "<<< RSTATFS fid %d type 0x%lx bsize %ld "
		"blocks %llu bfree %llu bavail %llu files %llu ffree %llu "
		"fsid %llu namelen %ld\n",
		fid->fid, (long unsigned int)sb->type, (long int)sb->bsize,
		sb->blocks, sb->bfree, sb->bavail, sb->files,  sb->ffree,
		sb->fsid, (long int)sb->namelen);

	p9_free_req(clnt, req);
error:
	return err;
}
EXPORT_SYMBOL(p9_client_statfs);

int p9_client_rename(struct p9_fid *fid,
		     struct p9_fid *newdirfid, const char *name)
{
	int err;
	struct p9_req_t *req;
	struct p9_client *clnt;

	err = 0;
	clnt = fid->clnt;

	p9_debug(P9_DEBUG_9P, ">>> TRENAME fid %d newdirfid %d name %s\n",
			fid->fid, newdirfid->fid, name);

	req = p9_client_rpc(clnt, P9_TRENAME, "dds", fid->fid,
			newdirfid->fid, name);
	if (IS_ERR(req)) {
		err = PTR_ERR(req);
		goto error;
	}

	p9_debug(P9_DEBUG_9P, "<<< RRENAME fid %d\n", fid->fid);

	p9_free_req(clnt, req);
error:
	return err;
}
EXPORT_SYMBOL(p9_client_rename);

int p9_client_renameat(struct p9_fid *olddirfid, const char *old_name,
		       struct p9_fid *newdirfid, const char *new_name)
{
	int err;
	struct p9_req_t *req;
	struct p9_client *clnt;

	err = 0;
	clnt = olddirfid->clnt;

	p9_debug(P9_DEBUG_9P, ">>> TRENAMEAT olddirfid %d old name %s"
		   " newdirfid %d new name %s\n", olddirfid->fid, old_name,
		   newdirfid->fid, new_name);

	req = p9_client_rpc(clnt, P9_TRENAMEAT, "dsds", olddirfid->fid,
			    old_name, newdirfid->fid, new_name);
	if (IS_ERR(req)) {
		err = PTR_ERR(req);
		goto error;
	}

	p9_debug(P9_DEBUG_9P, "<<< RRENAMEAT newdirfid %d new name %s\n",
		   newdirfid->fid, new_name);

	p9_free_req(clnt, req);
error:
	return err;
}
EXPORT_SYMBOL(p9_client_renameat);

/*
 * An xattrwalk without @attr_name gives the fid for the lisxattr namespace
 */
struct p9_fid *p9_client_xattrwalk(struct p9_fid *file_fid,
				const char *attr_name, u64 *attr_size)
{
	int err;
	struct p9_req_t *req;
	struct p9_client *clnt;
	struct p9_fid *attr_fid;

	err = 0;
	clnt = file_fid->clnt;
	attr_fid = p9_fid_create(clnt);
	if (IS_ERR(attr_fid)) {
		err = PTR_ERR(attr_fid);
		attr_fid = NULL;
		goto error;
	}
	p9_debug(P9_DEBUG_9P,
		">>> TXATTRWALK file_fid %d, attr_fid %d name %s\n",
		file_fid->fid, attr_fid->fid, attr_name);

	req = p9_client_rpc(clnt, P9_TXATTRWALK, "dds",
			file_fid->fid, attr_fid->fid, attr_name);
	if (IS_ERR(req)) {
		err = PTR_ERR(req);
		goto error;
	}
	err = p9pdu_readf(req->rc, clnt->proto_version, "q", attr_size);
	if (err) {
		trace_9p_protocol_dump(clnt, req->rc);
		p9_free_req(clnt, req);
		goto clunk_fid;
	}
	p9_free_req(clnt, req);
	p9_debug(P9_DEBUG_9P, "<<<  RXATTRWALK fid %d size %llu\n",
		attr_fid->fid, *attr_size);
	return attr_fid;
clunk_fid:
	p9_client_clunk(attr_fid);
	attr_fid = NULL;
error:
	if (attr_fid && (attr_fid != file_fid))
		p9_fid_destroy(attr_fid);

	return ERR_PTR(err);
}
EXPORT_SYMBOL_GPL(p9_client_xattrwalk);

int p9_client_xattrcreate(struct p9_fid *fid, const char *name,
			u64 attr_size, int flags)
{
	int err;
	struct p9_req_t *req;
	struct p9_client *clnt;

	p9_debug(P9_DEBUG_9P,
		">>> TXATTRCREATE fid %d name  %s size %lld flag %d\n",
		fid->fid, name, (long long)attr_size, flags);
	err = 0;
	clnt = fid->clnt;
	req = p9_client_rpc(clnt, P9_TXATTRCREATE, "dsqd",
			fid->fid, name, attr_size, flags);
	if (IS_ERR(req)) {
		err = PTR_ERR(req);
		goto error;
	}
	p9_debug(P9_DEBUG_9P, "<<< RXATTRCREATE fid %d\n", fid->fid);
	p9_free_req(clnt, req);
error:
	return err;
}
EXPORT_SYMBOL_GPL(p9_client_xattrcreate);

int p9_client_readdir(struct p9_fid *fid, char *data, u32 count, u64 offset)
{
	int err, rsize, non_zc = 0;
	struct p9_client *clnt;
	struct p9_req_t *req;
	char *dataptr;

	p9_debug(P9_DEBUG_9P, ">>> TREADDIR fid %d offset %llu count %d\n",
				fid->fid, (unsigned long long) offset, count);

	err = 0;
	clnt = fid->clnt;

	rsize = fid->iounit;
	if (!rsize || rsize > clnt->msize-P9_READDIRHDRSZ)
		rsize = clnt->msize - P9_READDIRHDRSZ;

	if (count < rsize)
		rsize = count;

	/* Don't bother zerocopy for small IO (< 1024) */
	if (clnt->trans_mod->zc_request && rsize > 1024) {
		/*
		 * response header len is 11
		 * PDU Header(7) + IO Size (4)
		 */
		req = p9_client_zc_rpc(clnt, P9_TREADDIR, data, NULL, rsize, 0,
				       11, 1, "dqd", fid->fid, offset, rsize);
	} else {
		non_zc = 1;
		req = p9_client_rpc(clnt, P9_TREADDIR, "dqd", fid->fid,
				    offset, rsize);
	}
	if (IS_ERR(req)) {
		err = PTR_ERR(req);
		goto error;
	}

	err = p9pdu_readf(req->rc, clnt->proto_version, "D", &count, &dataptr);
	if (err) {
		trace_9p_protocol_dump(clnt, req->rc);
		goto free_and_error;
	}

	p9_debug(P9_DEBUG_9P, "<<< RREADDIR count %d\n", count);

	if (non_zc)
		memmove(data, dataptr, count);

	p9_free_req(clnt, req);
	return count;

free_and_error:
	p9_free_req(clnt, req);
error:
	return err;
}
EXPORT_SYMBOL(p9_client_readdir);

int p9_client_mknod_dotl(struct p9_fid *fid, char *name, int mode,
			dev_t rdev, gid_t gid, struct p9_qid *qid)
{
	int err;
	struct p9_client *clnt;
	struct p9_req_t *req;

	err = 0;
	clnt = fid->clnt;
	p9_debug(P9_DEBUG_9P, ">>> TMKNOD fid %d name %s mode %d major %d "
		"minor %d\n", fid->fid, name, mode, MAJOR(rdev), MINOR(rdev));
	req = p9_client_rpc(clnt, P9_TMKNOD, "dsdddd", fid->fid, name, mode,
		MAJOR(rdev), MINOR(rdev), gid);
	if (IS_ERR(req))
		return PTR_ERR(req);

	err = p9pdu_readf(req->rc, clnt->proto_version, "Q", qid);
	if (err) {
		trace_9p_protocol_dump(clnt, req->rc);
		goto error;
	}
	p9_debug(P9_DEBUG_9P, "<<< RMKNOD qid %x.%llx.%x\n", qid->type,
				(unsigned long long)qid->path, qid->version);

error:
	p9_free_req(clnt, req);
	return err;

}
EXPORT_SYMBOL(p9_client_mknod_dotl);

int p9_client_mkdir_dotl(struct p9_fid *fid, char *name, int mode,
				gid_t gid, struct p9_qid *qid)
{
	int err;
	struct p9_client *clnt;
	struct p9_req_t *req;

	err = 0;
	clnt = fid->clnt;
	p9_debug(P9_DEBUG_9P, ">>> TMKDIR fid %d name %s mode %d gid %d\n",
		 fid->fid, name, mode, gid);
	req = p9_client_rpc(clnt, P9_TMKDIR, "dsdd", fid->fid, name, mode,
		gid);
	if (IS_ERR(req))
		return PTR_ERR(req);

	err = p9pdu_readf(req->rc, clnt->proto_version, "Q", qid);
	if (err) {
		trace_9p_protocol_dump(clnt, req->rc);
		goto error;
	}
	p9_debug(P9_DEBUG_9P, "<<< RMKDIR qid %x.%llx.%x\n", qid->type,
				(unsigned long long)qid->path, qid->version);

error:
	p9_free_req(clnt, req);
	return err;

}
EXPORT_SYMBOL(p9_client_mkdir_dotl);

int p9_client_lock_dotl(struct p9_fid *fid, struct p9_flock *flock, u8 *status)
{
	int err;
	struct p9_client *clnt;
	struct p9_req_t *req;

	err = 0;
	clnt = fid->clnt;
	p9_debug(P9_DEBUG_9P, ">>> TLOCK fid %d type %i flags %d "
			"start %lld length %lld proc_id %d client_id %s\n",
			fid->fid, flock->type, flock->flags, flock->start,
			flock->length, flock->proc_id, flock->client_id);

	req = p9_client_rpc(clnt, P9_TLOCK, "dbdqqds", fid->fid, flock->type,
				flock->flags, flock->start, flock->length,
					flock->proc_id, flock->client_id);

	if (IS_ERR(req))
		return PTR_ERR(req);

	err = p9pdu_readf(req->rc, clnt->proto_version, "b", status);
	if (err) {
		trace_9p_protocol_dump(clnt, req->rc);
		goto error;
	}
	p9_debug(P9_DEBUG_9P, "<<< RLOCK status %i\n", *status);
error:
	p9_free_req(clnt, req);
	return err;

}
EXPORT_SYMBOL(p9_client_lock_dotl);

int p9_client_getlock_dotl(struct p9_fid *fid, struct p9_getlock *glock)
{
	int err;
	struct p9_client *clnt;
	struct p9_req_t *req;

	err = 0;
	clnt = fid->clnt;
	p9_debug(P9_DEBUG_9P, ">>> TGETLOCK fid %d, type %i start %lld "
		"length %lld proc_id %d client_id %s\n", fid->fid, glock->type,
		glock->start, glock->length, glock->proc_id, glock->client_id);

	req = p9_client_rpc(clnt, P9_TGETLOCK, "dbqqds", fid->fid,  glock->type,
		glock->start, glock->length, glock->proc_id, glock->client_id);

	if (IS_ERR(req))
		return PTR_ERR(req);

	err = p9pdu_readf(req->rc, clnt->proto_version, "bqqds", &glock->type,
			&glock->start, &glock->length, &glock->proc_id,
			&glock->client_id);
	if (err) {
		trace_9p_protocol_dump(clnt, req->rc);
		goto error;
	}
	p9_debug(P9_DEBUG_9P, "<<< RGETLOCK type %i start %lld length %lld "
		"proc_id %d client_id %s\n", glock->type, glock->start,
		glock->length, glock->proc_id, glock->client_id);
error:
	p9_free_req(clnt, req);
	return err;
}
EXPORT_SYMBOL(p9_client_getlock_dotl);

int p9_client_readlink(struct p9_fid *fid, char **target)
{
	int err;
	struct p9_client *clnt;
	struct p9_req_t *req;

	err = 0;
	clnt = fid->clnt;
	p9_debug(P9_DEBUG_9P, ">>> TREADLINK fid %d\n", fid->fid);

	req = p9_client_rpc(clnt, P9_TREADLINK, "d", fid->fid);
	if (IS_ERR(req))
		return PTR_ERR(req);

	err = p9pdu_readf(req->rc, clnt->proto_version, "s", target);
	if (err) {
		trace_9p_protocol_dump(clnt, req->rc);
		goto error;
	}
	p9_debug(P9_DEBUG_9P, "<<< RREADLINK target %s\n", *target);
error:
	p9_free_req(clnt, req);
	return err;
}
EXPORT_SYMBOL(p9_client_readlink);
