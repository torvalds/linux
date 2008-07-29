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
#include <net/9p/transport.h>
#include <net/9p/client.h>

static struct p9_fid *p9_fid_create(struct p9_client *clnt);
static void p9_fid_destroy(struct p9_fid *fid);
static struct p9_stat *p9_clone_stat(struct p9_stat *st, int dotu);

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

static match_table_t tokens = {
	{Opt_msize, "msize=%u"},
	{Opt_legacy, "noextend"},
	{Opt_trans, "trans=%s"},
	{Opt_err, NULL},
};

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

	clnt->trans_mod = v9fs_default_trans();
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
			clnt->trans_mod = v9fs_match_trans(&args[0]);
			break;
		case Opt_legacy:
			clnt->dotu = 0;
			break;
		default:
			continue;
		}
	}
	kfree(options);
	return ret;
}


/**
 * p9_client_rpc - sends 9P request and waits until a response is available.
 *      The function can be interrupted.
 * @c: client data
 * @tc: request to be sent
 * @rc: pointer where a pointer to the response is stored
 */
int
p9_client_rpc(struct p9_client *c, struct p9_fcall *tc,
	struct p9_fcall **rc)
{
	return c->trans->rpc(c->trans, tc, rc);
}

struct p9_client *p9_client_create(const char *dev_name, char *options)
{
	int err, n;
	struct p9_client *clnt;
	struct p9_fcall *tc, *rc;
	struct p9_str *version;

	err = 0;
	tc = NULL;
	rc = NULL;
	clnt = kmalloc(sizeof(struct p9_client), GFP_KERNEL);
	if (!clnt)
		return ERR_PTR(-ENOMEM);

	clnt->trans = NULL;
	spin_lock_init(&clnt->lock);
	INIT_LIST_HEAD(&clnt->fidlist);
	clnt->fidpool = p9_idpool_create();
	if (IS_ERR(clnt->fidpool)) {
		err = PTR_ERR(clnt->fidpool);
		clnt->fidpool = NULL;
		goto error;
	}

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


	clnt->trans = clnt->trans_mod->create(dev_name, options, clnt->msize,
								clnt->dotu);
	if (IS_ERR(clnt->trans)) {
		err = PTR_ERR(clnt->trans);
		clnt->trans = NULL;
		goto error;
	}

	if ((clnt->msize+P9_IOHDRSZ) > clnt->trans_mod->maxsize)
		clnt->msize = clnt->trans_mod->maxsize-P9_IOHDRSZ;

	tc = p9_create_tversion(clnt->msize, clnt->dotu?"9P2000.u":"9P2000");
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

	n = rc->params.rversion.msize;
	if (n < clnt->msize)
		clnt->msize = n;

	kfree(tc);
	kfree(rc);
	return clnt;

error:
	kfree(tc);
	kfree(rc);
	p9_client_destroy(clnt);
	return ERR_PTR(err);
}
EXPORT_SYMBOL(p9_client_create);

void p9_client_destroy(struct p9_client *clnt)
{
	struct p9_fid *fid, *fidptr;

	P9_DPRINTK(P9_DEBUG_9P, "clnt %p\n", clnt);

	if (clnt->trans) {
		clnt->trans->close(clnt->trans);
		kfree(clnt->trans);
		clnt->trans = NULL;
	}

	list_for_each_entry_safe(fid, fidptr, &clnt->fidlist, flist)
		p9_fid_destroy(fid);

	if (clnt->fidpool)
		p9_idpool_destroy(clnt->fidpool);

	kfree(clnt);
}
EXPORT_SYMBOL(p9_client_destroy);

void p9_client_disconnect(struct p9_client *clnt)
{
	P9_DPRINTK(P9_DEBUG_9P, "clnt %p\n", clnt);
	clnt->trans->status = Disconnected;
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

int p9_client_read(struct p9_fid *fid, char *data, u64 offset, u32 count)
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

		memmove(data, rc->params.rread.data, n);
		count -= n;
		data += n;
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

int p9_client_write(struct p9_fid *fid, char *data, u64 offset, u32 count)
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

		tc = p9_create_twrite(fid->fid, offset, rsize, data);
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
		data += n;
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

int
p9_client_uread(struct p9_fid *fid, char __user *data, u64 offset, u32 count)
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

		err = copy_to_user(data, rc->params.rread.data, n);
		if (err) {
			err = -EFAULT;
			goto error;
		}

		count -= n;
		data += n;
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
EXPORT_SYMBOL(p9_client_uread);

int
p9_client_uwrite(struct p9_fid *fid, const char __user *data, u64 offset,
								   u32 count)
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

		tc = p9_create_twrite_u(fid->fid, offset, rsize, data);
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
		data += n;
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
EXPORT_SYMBOL(p9_client_uwrite);

int p9_client_readn(struct p9_fid *fid, char *data, u64 offset, u32 count)
{
	int n, total;

	P9_DPRINTK(P9_DEBUG_9P, "fid %d offset %llu count %d\n", fid->fid,
					(long long unsigned) offset, count);
	n = 0;
	total = 0;
	while (count) {
		n = p9_client_read(fid, data, offset, count);
		if (n <= 0)
			break;

		data += n;
		offset += n;
		count -= n;
		total += n;
	}

	if (n < 0)
		total = n;

	return total;
}
EXPORT_SYMBOL(p9_client_readn);

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

struct p9_stat *p9_client_dirread(struct p9_fid *fid, u64 offset)
{
	int err, n, m;
	struct p9_fcall *tc, *rc;
	struct p9_client *clnt;
	struct p9_stat st, *ret;

	P9_DPRINTK(P9_DEBUG_9P, "fid %d offset %llu\n", fid->fid,
						(long long unsigned) offset);
	err = 0;
	tc = NULL;
	rc = NULL;
	ret = NULL;
	clnt = fid->clnt;

	/* if the offset is below or above the current response, free it */
	if (offset < fid->rdir_fpos || (fid->rdir_fcall &&
		offset >= fid->rdir_fpos+fid->rdir_fcall->params.rread.count)) {
		fid->rdir_pos = 0;
		if (fid->rdir_fcall)
			fid->rdir_fpos += fid->rdir_fcall->params.rread.count;

		kfree(fid->rdir_fcall);
		fid->rdir_fcall = NULL;
		if (offset < fid->rdir_fpos)
			fid->rdir_fpos = 0;
	}

	if (!fid->rdir_fcall) {
		n = fid->iounit;
		if (!n || n > clnt->msize-P9_IOHDRSZ)
			n = clnt->msize - P9_IOHDRSZ;

		while (1) {
			if (fid->rdir_fcall) {
				fid->rdir_fpos +=
					fid->rdir_fcall->params.rread.count;
				kfree(fid->rdir_fcall);
				fid->rdir_fcall = NULL;
			}

			tc = p9_create_tread(fid->fid, fid->rdir_fpos, n);
			if (IS_ERR(tc)) {
				err = PTR_ERR(tc);
				tc = NULL;
				goto error;
			}

			err = p9_client_rpc(clnt, tc, &rc);
			if (err)
				goto error;

			n = rc->params.rread.count;
			if (n == 0)
				goto done;

			fid->rdir_fcall = rc;
			rc = NULL;
			if (offset >= fid->rdir_fpos &&
						offset < fid->rdir_fpos+n)
				break;
		}

		fid->rdir_pos = 0;
	}

	m = offset - fid->rdir_fpos;
	if (m < 0)
		goto done;

	n = p9_deserialize_stat(fid->rdir_fcall->params.rread.data + m,
		fid->rdir_fcall->params.rread.count - m, &st, clnt->dotu);

	if (!n) {
		err = -EIO;
		goto error;
	}

	fid->rdir_pos += n;
	st.size = n;
	ret = p9_clone_stat(&st, clnt->dotu);
	if (IS_ERR(ret)) {
		err = PTR_ERR(ret);
		ret = NULL;
		goto error;
	}

done:
	kfree(tc);
	kfree(rc);
	return ret;

error:
	kfree(tc);
	kfree(rc);
	kfree(ret);
	return ERR_PTR(err);
}
EXPORT_SYMBOL(p9_client_dirread);

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
	fid->rdir_pos = 0;
	fid->rdir_fcall = NULL;
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
	kfree(fid->rdir_fcall);
	kfree(fid);
}
