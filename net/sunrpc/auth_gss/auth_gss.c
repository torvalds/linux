/*
 * linux/net/sunrpc/auth_gss/auth_gss.c
 *
 * RPCSEC_GSS client authentication.
 *
 *  Copyright (c) 2000 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Dug Song       <dugsong@monkey.org>
 *  Andy Adamson   <andros@umich.edu>
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. Neither the name of the University nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 *  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 *  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $Id$
 */


#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/pagemap.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/auth.h>
#include <linux/sunrpc/auth_gss.h>
#include <linux/sunrpc/svcauth_gss.h>
#include <linux/sunrpc/gss_err.h>
#include <linux/workqueue.h>
#include <linux/sunrpc/rpc_pipe_fs.h>
#include <linux/sunrpc/gss_api.h>
#include <asm/uaccess.h>

static const struct rpc_authops authgss_ops;

static const struct rpc_credops gss_credops;
static const struct rpc_credops gss_nullops;

#ifdef RPC_DEBUG
# define RPCDBG_FACILITY	RPCDBG_AUTH
#endif

#define NFS_NGROUPS	16

#define GSS_CRED_SLACK		1024		/* XXX: unused */
/* length of a krb5 verifier (48), plus data added before arguments when
 * using integrity (two 4-byte integers): */
#define GSS_VERF_SLACK		100

/* XXX this define must match the gssd define
* as it is passed to gssd to signal the use of
* machine creds should be part of the shared rpc interface */

#define CA_RUN_AS_MACHINE  0x00000200

/* dump the buffer in `emacs-hexl' style */
#define isprint(c)      ((c > 0x1f) && (c < 0x7f))

struct gss_auth {
	struct kref kref;
	struct rpc_auth rpc_auth;
	struct gss_api_mech *mech;
	enum rpc_gss_svc service;
	struct rpc_clnt *client;
	struct dentry *dentry;
};

static void gss_free_ctx(struct gss_cl_ctx *);
static struct rpc_pipe_ops gss_upcall_ops;

static inline struct gss_cl_ctx *
gss_get_ctx(struct gss_cl_ctx *ctx)
{
	atomic_inc(&ctx->count);
	return ctx;
}

static inline void
gss_put_ctx(struct gss_cl_ctx *ctx)
{
	if (atomic_dec_and_test(&ctx->count))
		gss_free_ctx(ctx);
}

/* gss_cred_set_ctx:
 * called by gss_upcall_callback and gss_create_upcall in order
 * to set the gss context. The actual exchange of an old context
 * and a new one is protected by the inode->i_lock.
 */
static void
gss_cred_set_ctx(struct rpc_cred *cred, struct gss_cl_ctx *ctx)
{
	struct gss_cred *gss_cred = container_of(cred, struct gss_cred, gc_base);
	struct gss_cl_ctx *old;

	old = gss_cred->gc_ctx;
	rcu_assign_pointer(gss_cred->gc_ctx, ctx);
	set_bit(RPCAUTH_CRED_UPTODATE, &cred->cr_flags);
	clear_bit(RPCAUTH_CRED_NEW, &cred->cr_flags);
	if (old)
		gss_put_ctx(old);
}

static int
gss_cred_is_uptodate_ctx(struct rpc_cred *cred)
{
	struct gss_cred *gss_cred = container_of(cred, struct gss_cred, gc_base);
	int res = 0;

	rcu_read_lock();
	if (test_bit(RPCAUTH_CRED_UPTODATE, &cred->cr_flags) && gss_cred->gc_ctx)
		res = 1;
	rcu_read_unlock();
	return res;
}

static const void *
simple_get_bytes(const void *p, const void *end, void *res, size_t len)
{
	const void *q = (const void *)((const char *)p + len);
	if (unlikely(q > end || q < p))
		return ERR_PTR(-EFAULT);
	memcpy(res, p, len);
	return q;
}

static inline const void *
simple_get_netobj(const void *p, const void *end, struct xdr_netobj *dest)
{
	const void *q;
	unsigned int len;

	p = simple_get_bytes(p, end, &len, sizeof(len));
	if (IS_ERR(p))
		return p;
	q = (const void *)((const char *)p + len);
	if (unlikely(q > end || q < p))
		return ERR_PTR(-EFAULT);
	dest->data = kmemdup(p, len, GFP_KERNEL);
	if (unlikely(dest->data == NULL))
		return ERR_PTR(-ENOMEM);
	dest->len = len;
	return q;
}

static struct gss_cl_ctx *
gss_cred_get_ctx(struct rpc_cred *cred)
{
	struct gss_cred *gss_cred = container_of(cred, struct gss_cred, gc_base);
	struct gss_cl_ctx *ctx = NULL;

	rcu_read_lock();
	if (gss_cred->gc_ctx)
		ctx = gss_get_ctx(gss_cred->gc_ctx);
	rcu_read_unlock();
	return ctx;
}

static struct gss_cl_ctx *
gss_alloc_context(void)
{
	struct gss_cl_ctx *ctx;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (ctx != NULL) {
		ctx->gc_proc = RPC_GSS_PROC_DATA;
		ctx->gc_seq = 1;	/* NetApp 6.4R1 doesn't accept seq. no. 0 */
		spin_lock_init(&ctx->gc_seq_lock);
		atomic_set(&ctx->count,1);
	}
	return ctx;
}

#define GSSD_MIN_TIMEOUT (60 * 60)
static const void *
gss_fill_context(const void *p, const void *end, struct gss_cl_ctx *ctx, struct gss_api_mech *gm)
{
	const void *q;
	unsigned int seclen;
	unsigned int timeout;
	u32 window_size;
	int ret;

	/* First unsigned int gives the lifetime (in seconds) of the cred */
	p = simple_get_bytes(p, end, &timeout, sizeof(timeout));
	if (IS_ERR(p))
		goto err;
	if (timeout == 0)
		timeout = GSSD_MIN_TIMEOUT;
	ctx->gc_expiry = jiffies + (unsigned long)timeout * HZ * 3 / 4;
	/* Sequence number window. Determines the maximum number of simultaneous requests */
	p = simple_get_bytes(p, end, &window_size, sizeof(window_size));
	if (IS_ERR(p))
		goto err;
	ctx->gc_win = window_size;
	/* gssd signals an error by passing ctx->gc_win = 0: */
	if (ctx->gc_win == 0) {
		/* in which case, p points to  an error code which we ignore */
		p = ERR_PTR(-EACCES);
		goto err;
	}
	/* copy the opaque wire context */
	p = simple_get_netobj(p, end, &ctx->gc_wire_ctx);
	if (IS_ERR(p))
		goto err;
	/* import the opaque security context */
	p  = simple_get_bytes(p, end, &seclen, sizeof(seclen));
	if (IS_ERR(p))
		goto err;
	q = (const void *)((const char *)p + seclen);
	if (unlikely(q > end || q < p)) {
		p = ERR_PTR(-EFAULT);
		goto err;
	}
	ret = gss_import_sec_context(p, seclen, gm, &ctx->gc_gss_ctx);
	if (ret < 0) {
		p = ERR_PTR(ret);
		goto err;
	}
	return q;
err:
	dprintk("RPC:       gss_fill_context returning %ld\n", -PTR_ERR(p));
	return p;
}


struct gss_upcall_msg {
	atomic_t count;
	uid_t	uid;
	struct rpc_pipe_msg msg;
	struct list_head list;
	struct gss_auth *auth;
	struct rpc_wait_queue rpc_waitqueue;
	wait_queue_head_t waitqueue;
	struct gss_cl_ctx *ctx;
};

static void
gss_release_msg(struct gss_upcall_msg *gss_msg)
{
	if (!atomic_dec_and_test(&gss_msg->count))
		return;
	BUG_ON(!list_empty(&gss_msg->list));
	if (gss_msg->ctx != NULL)
		gss_put_ctx(gss_msg->ctx);
	kfree(gss_msg);
}

static struct gss_upcall_msg *
__gss_find_upcall(struct rpc_inode *rpci, uid_t uid)
{
	struct gss_upcall_msg *pos;
	list_for_each_entry(pos, &rpci->in_downcall, list) {
		if (pos->uid != uid)
			continue;
		atomic_inc(&pos->count);
		dprintk("RPC:       gss_find_upcall found msg %p\n", pos);
		return pos;
	}
	dprintk("RPC:       gss_find_upcall found nothing\n");
	return NULL;
}

/* Try to add a upcall to the pipefs queue.
 * If an upcall owned by our uid already exists, then we return a reference
 * to that upcall instead of adding the new upcall.
 */
static inline struct gss_upcall_msg *
gss_add_msg(struct gss_auth *gss_auth, struct gss_upcall_msg *gss_msg)
{
	struct inode *inode = gss_auth->dentry->d_inode;
	struct rpc_inode *rpci = RPC_I(inode);
	struct gss_upcall_msg *old;

	spin_lock(&inode->i_lock);
	old = __gss_find_upcall(rpci, gss_msg->uid);
	if (old == NULL) {
		atomic_inc(&gss_msg->count);
		list_add(&gss_msg->list, &rpci->in_downcall);
	} else
		gss_msg = old;
	spin_unlock(&inode->i_lock);
	return gss_msg;
}

static void
__gss_unhash_msg(struct gss_upcall_msg *gss_msg)
{
	list_del_init(&gss_msg->list);
	rpc_wake_up_status(&gss_msg->rpc_waitqueue, gss_msg->msg.errno);
	wake_up_all(&gss_msg->waitqueue);
	atomic_dec(&gss_msg->count);
}

static void
gss_unhash_msg(struct gss_upcall_msg *gss_msg)
{
	struct gss_auth *gss_auth = gss_msg->auth;
	struct inode *inode = gss_auth->dentry->d_inode;

	if (list_empty(&gss_msg->list))
		return;
	spin_lock(&inode->i_lock);
	if (!list_empty(&gss_msg->list))
		__gss_unhash_msg(gss_msg);
	spin_unlock(&inode->i_lock);
}

static void
gss_upcall_callback(struct rpc_task *task)
{
	struct gss_cred *gss_cred = container_of(task->tk_msg.rpc_cred,
			struct gss_cred, gc_base);
	struct gss_upcall_msg *gss_msg = gss_cred->gc_upcall;
	struct inode *inode = gss_msg->auth->dentry->d_inode;

	spin_lock(&inode->i_lock);
	if (gss_msg->ctx)
		gss_cred_set_ctx(task->tk_msg.rpc_cred, gss_get_ctx(gss_msg->ctx));
	else
		task->tk_status = gss_msg->msg.errno;
	gss_cred->gc_upcall = NULL;
	rpc_wake_up_status(&gss_msg->rpc_waitqueue, gss_msg->msg.errno);
	spin_unlock(&inode->i_lock);
	gss_release_msg(gss_msg);
}

static inline struct gss_upcall_msg *
gss_alloc_msg(struct gss_auth *gss_auth, uid_t uid)
{
	struct gss_upcall_msg *gss_msg;

	gss_msg = kzalloc(sizeof(*gss_msg), GFP_KERNEL);
	if (gss_msg != NULL) {
		INIT_LIST_HEAD(&gss_msg->list);
		rpc_init_wait_queue(&gss_msg->rpc_waitqueue, "RPCSEC_GSS upcall waitq");
		init_waitqueue_head(&gss_msg->waitqueue);
		atomic_set(&gss_msg->count, 1);
		gss_msg->msg.data = &gss_msg->uid;
		gss_msg->msg.len = sizeof(gss_msg->uid);
		gss_msg->uid = uid;
		gss_msg->auth = gss_auth;
	}
	return gss_msg;
}

static struct gss_upcall_msg *
gss_setup_upcall(struct rpc_clnt *clnt, struct gss_auth *gss_auth, struct rpc_cred *cred)
{
	struct gss_upcall_msg *gss_new, *gss_msg;

	gss_new = gss_alloc_msg(gss_auth, cred->cr_uid);
	if (gss_new == NULL)
		return ERR_PTR(-ENOMEM);
	gss_msg = gss_add_msg(gss_auth, gss_new);
	if (gss_msg == gss_new) {
		int res = rpc_queue_upcall(gss_auth->dentry->d_inode, &gss_new->msg);
		if (res) {
			gss_unhash_msg(gss_new);
			gss_msg = ERR_PTR(res);
		}
	} else
		gss_release_msg(gss_new);
	return gss_msg;
}

static inline int
gss_refresh_upcall(struct rpc_task *task)
{
	struct rpc_cred *cred = task->tk_msg.rpc_cred;
	struct gss_auth *gss_auth = container_of(cred->cr_auth,
			struct gss_auth, rpc_auth);
	struct gss_cred *gss_cred = container_of(cred,
			struct gss_cred, gc_base);
	struct gss_upcall_msg *gss_msg;
	struct inode *inode = gss_auth->dentry->d_inode;
	int err = 0;

	dprintk("RPC: %5u gss_refresh_upcall for uid %u\n", task->tk_pid,
								cred->cr_uid);
	gss_msg = gss_setup_upcall(task->tk_client, gss_auth, cred);
	if (IS_ERR(gss_msg)) {
		err = PTR_ERR(gss_msg);
		goto out;
	}
	spin_lock(&inode->i_lock);
	if (gss_cred->gc_upcall != NULL)
		rpc_sleep_on(&gss_cred->gc_upcall->rpc_waitqueue, task, NULL, NULL);
	else if (gss_msg->ctx == NULL && gss_msg->msg.errno >= 0) {
		task->tk_timeout = 0;
		gss_cred->gc_upcall = gss_msg;
		/* gss_upcall_callback will release the reference to gss_upcall_msg */
		atomic_inc(&gss_msg->count);
		rpc_sleep_on(&gss_msg->rpc_waitqueue, task, gss_upcall_callback, NULL);
	} else
		err = gss_msg->msg.errno;
	spin_unlock(&inode->i_lock);
	gss_release_msg(gss_msg);
out:
	dprintk("RPC: %5u gss_refresh_upcall for uid %u result %d\n",
			task->tk_pid, cred->cr_uid, err);
	return err;
}

static inline int
gss_create_upcall(struct gss_auth *gss_auth, struct gss_cred *gss_cred)
{
	struct inode *inode = gss_auth->dentry->d_inode;
	struct rpc_cred *cred = &gss_cred->gc_base;
	struct gss_upcall_msg *gss_msg;
	DEFINE_WAIT(wait);
	int err = 0;

	dprintk("RPC:       gss_upcall for uid %u\n", cred->cr_uid);
	gss_msg = gss_setup_upcall(gss_auth->client, gss_auth, cred);
	if (IS_ERR(gss_msg)) {
		err = PTR_ERR(gss_msg);
		goto out;
	}
	for (;;) {
		prepare_to_wait(&gss_msg->waitqueue, &wait, TASK_INTERRUPTIBLE);
		spin_lock(&inode->i_lock);
		if (gss_msg->ctx != NULL || gss_msg->msg.errno < 0) {
			break;
		}
		spin_unlock(&inode->i_lock);
		if (signalled()) {
			err = -ERESTARTSYS;
			goto out_intr;
		}
		schedule();
	}
	if (gss_msg->ctx)
		gss_cred_set_ctx(cred, gss_get_ctx(gss_msg->ctx));
	else
		err = gss_msg->msg.errno;
	spin_unlock(&inode->i_lock);
out_intr:
	finish_wait(&gss_msg->waitqueue, &wait);
	gss_release_msg(gss_msg);
out:
	dprintk("RPC:       gss_create_upcall for uid %u result %d\n",
			cred->cr_uid, err);
	return err;
}

static ssize_t
gss_pipe_upcall(struct file *filp, struct rpc_pipe_msg *msg,
		char __user *dst, size_t buflen)
{
	char *data = (char *)msg->data + msg->copied;
	size_t mlen = min(msg->len, buflen);
	unsigned long left;

	left = copy_to_user(dst, data, mlen);
	if (left == mlen) {
		msg->errno = -EFAULT;
		return -EFAULT;
	}

	mlen -= left;
	msg->copied += mlen;
	msg->errno = 0;
	return mlen;
}

#define MSG_BUF_MAXSIZE 1024

static ssize_t
gss_pipe_downcall(struct file *filp, const char __user *src, size_t mlen)
{
	const void *p, *end;
	void *buf;
	struct rpc_clnt *clnt;
	struct gss_upcall_msg *gss_msg;
	struct inode *inode = filp->f_path.dentry->d_inode;
	struct gss_cl_ctx *ctx;
	uid_t uid;
	ssize_t err = -EFBIG;

	if (mlen > MSG_BUF_MAXSIZE)
		goto out;
	err = -ENOMEM;
	buf = kmalloc(mlen, GFP_KERNEL);
	if (!buf)
		goto out;

	clnt = RPC_I(inode)->private;
	err = -EFAULT;
	if (copy_from_user(buf, src, mlen))
		goto err;

	end = (const void *)((char *)buf + mlen);
	p = simple_get_bytes(buf, end, &uid, sizeof(uid));
	if (IS_ERR(p)) {
		err = PTR_ERR(p);
		goto err;
	}

	err = -ENOMEM;
	ctx = gss_alloc_context();
	if (ctx == NULL)
		goto err;

	err = -ENOENT;
	/* Find a matching upcall */
	spin_lock(&inode->i_lock);
	gss_msg = __gss_find_upcall(RPC_I(inode), uid);
	if (gss_msg == NULL) {
		spin_unlock(&inode->i_lock);
		goto err_put_ctx;
	}
	list_del_init(&gss_msg->list);
	spin_unlock(&inode->i_lock);

	p = gss_fill_context(p, end, ctx, gss_msg->auth->mech);
	if (IS_ERR(p)) {
		err = PTR_ERR(p);
		gss_msg->msg.errno = (err == -EAGAIN) ? -EAGAIN : -EACCES;
		goto err_release_msg;
	}
	gss_msg->ctx = gss_get_ctx(ctx);
	err = mlen;

err_release_msg:
	spin_lock(&inode->i_lock);
	__gss_unhash_msg(gss_msg);
	spin_unlock(&inode->i_lock);
	gss_release_msg(gss_msg);
err_put_ctx:
	gss_put_ctx(ctx);
err:
	kfree(buf);
out:
	dprintk("RPC:       gss_pipe_downcall returning %Zd\n", err);
	return err;
}

static void
gss_pipe_release(struct inode *inode)
{
	struct rpc_inode *rpci = RPC_I(inode);
	struct gss_upcall_msg *gss_msg;

	spin_lock(&inode->i_lock);
	while (!list_empty(&rpci->in_downcall)) {

		gss_msg = list_entry(rpci->in_downcall.next,
				struct gss_upcall_msg, list);
		gss_msg->msg.errno = -EPIPE;
		atomic_inc(&gss_msg->count);
		__gss_unhash_msg(gss_msg);
		spin_unlock(&inode->i_lock);
		gss_release_msg(gss_msg);
		spin_lock(&inode->i_lock);
	}
	spin_unlock(&inode->i_lock);
}

static void
gss_pipe_destroy_msg(struct rpc_pipe_msg *msg)
{
	struct gss_upcall_msg *gss_msg = container_of(msg, struct gss_upcall_msg, msg);
	static unsigned long ratelimit;

	if (msg->errno < 0) {
		dprintk("RPC:       gss_pipe_destroy_msg releasing msg %p\n",
				gss_msg);
		atomic_inc(&gss_msg->count);
		gss_unhash_msg(gss_msg);
		if (msg->errno == -ETIMEDOUT) {
			unsigned long now = jiffies;
			if (time_after(now, ratelimit)) {
				printk(KERN_WARNING "RPC: AUTH_GSS upcall timed out.\n"
						    "Please check user daemon is running!\n");
				ratelimit = now + 15*HZ;
			}
		}
		gss_release_msg(gss_msg);
	}
}

/*
 * NOTE: we have the opportunity to use different
 * parameters based on the input flavor (which must be a pseudoflavor)
 */
static struct rpc_auth *
gss_create(struct rpc_clnt *clnt, rpc_authflavor_t flavor)
{
	struct gss_auth *gss_auth;
	struct rpc_auth * auth;
	int err = -ENOMEM; /* XXX? */

	dprintk("RPC:       creating GSS authenticator for client %p\n", clnt);

	if (!try_module_get(THIS_MODULE))
		return ERR_PTR(err);
	if (!(gss_auth = kmalloc(sizeof(*gss_auth), GFP_KERNEL)))
		goto out_dec;
	gss_auth->client = clnt;
	err = -EINVAL;
	gss_auth->mech = gss_mech_get_by_pseudoflavor(flavor);
	if (!gss_auth->mech) {
		printk(KERN_WARNING "%s: Pseudoflavor %d not found!\n",
				__FUNCTION__, flavor);
		goto err_free;
	}
	gss_auth->service = gss_pseudoflavor_to_service(gss_auth->mech, flavor);
	if (gss_auth->service == 0)
		goto err_put_mech;
	auth = &gss_auth->rpc_auth;
	auth->au_cslack = GSS_CRED_SLACK >> 2;
	auth->au_rslack = GSS_VERF_SLACK >> 2;
	auth->au_ops = &authgss_ops;
	auth->au_flavor = flavor;
	atomic_set(&auth->au_count, 1);
	kref_init(&gss_auth->kref);

	gss_auth->dentry = rpc_mkpipe(clnt->cl_dentry, gss_auth->mech->gm_name,
			clnt, &gss_upcall_ops, RPC_PIPE_WAIT_FOR_OPEN);
	if (IS_ERR(gss_auth->dentry)) {
		err = PTR_ERR(gss_auth->dentry);
		goto err_put_mech;
	}

	err = rpcauth_init_credcache(auth);
	if (err)
		goto err_unlink_pipe;

	return auth;
err_unlink_pipe:
	rpc_unlink(gss_auth->dentry);
err_put_mech:
	gss_mech_put(gss_auth->mech);
err_free:
	kfree(gss_auth);
out_dec:
	module_put(THIS_MODULE);
	return ERR_PTR(err);
}

static void
gss_free(struct gss_auth *gss_auth)
{
	rpc_unlink(gss_auth->dentry);
	gss_auth->dentry = NULL;
	gss_mech_put(gss_auth->mech);

	kfree(gss_auth);
	module_put(THIS_MODULE);
}

static void
gss_free_callback(struct kref *kref)
{
	struct gss_auth *gss_auth = container_of(kref, struct gss_auth, kref);

	gss_free(gss_auth);
}

static void
gss_destroy(struct rpc_auth *auth)
{
	struct gss_auth *gss_auth;

	dprintk("RPC:       destroying GSS authenticator %p flavor %d\n",
			auth, auth->au_flavor);

	rpcauth_destroy_credcache(auth);

	gss_auth = container_of(auth, struct gss_auth, rpc_auth);
	kref_put(&gss_auth->kref, gss_free_callback);
}

/*
 * gss_destroying_context will cause the RPCSEC_GSS to send a NULL RPC call
 * to the server with the GSS control procedure field set to
 * RPC_GSS_PROC_DESTROY. This should normally cause the server to release
 * all RPCSEC_GSS state associated with that context.
 */
static int
gss_destroying_context(struct rpc_cred *cred)
{
	struct gss_cred *gss_cred = container_of(cred, struct gss_cred, gc_base);
	struct gss_auth *gss_auth = container_of(cred->cr_auth, struct gss_auth, rpc_auth);
	struct rpc_task *task;

	if (gss_cred->gc_ctx == NULL ||
			gss_cred->gc_ctx->gc_proc == RPC_GSS_PROC_DESTROY)
		return 0;

	gss_cred->gc_ctx->gc_proc = RPC_GSS_PROC_DESTROY;
	cred->cr_ops = &gss_nullops;

	/* Take a reference to ensure the cred will be destroyed either
	 * by the RPC call or by the put_rpccred() below */
	get_rpccred(cred);

	task = rpc_call_null(gss_auth->client, cred, RPC_TASK_ASYNC);
	if (!IS_ERR(task))
		rpc_put_task(task);

	put_rpccred(cred);
	return 1;
}

/* gss_destroy_cred (and gss_free_ctx) are used to clean up after failure
 * to create a new cred or context, so they check that things have been
 * allocated before freeing them. */
static void
gss_do_free_ctx(struct gss_cl_ctx *ctx)
{
	dprintk("RPC:       gss_free_ctx\n");

	kfree(ctx->gc_wire_ctx.data);
	kfree(ctx);
}

static void
gss_free_ctx_callback(struct rcu_head *head)
{
	struct gss_cl_ctx *ctx = container_of(head, struct gss_cl_ctx, gc_rcu);
	gss_do_free_ctx(ctx);
}

static void
gss_free_ctx(struct gss_cl_ctx *ctx)
{
	struct gss_ctx *gc_gss_ctx;

	gc_gss_ctx = rcu_dereference(ctx->gc_gss_ctx);
	rcu_assign_pointer(ctx->gc_gss_ctx, NULL);
	call_rcu(&ctx->gc_rcu, gss_free_ctx_callback);
	if (gc_gss_ctx)
		gss_delete_sec_context(&gc_gss_ctx);
}

static void
gss_free_cred(struct gss_cred *gss_cred)
{
	dprintk("RPC:       gss_free_cred %p\n", gss_cred);
	kfree(gss_cred);
}

static void
gss_free_cred_callback(struct rcu_head *head)
{
	struct gss_cred *gss_cred = container_of(head, struct gss_cred, gc_base.cr_rcu);
	gss_free_cred(gss_cred);
}

static void
gss_destroy_cred(struct rpc_cred *cred)
{
	struct gss_cred *gss_cred = container_of(cred, struct gss_cred, gc_base);
	struct gss_auth *gss_auth = container_of(cred->cr_auth, struct gss_auth, rpc_auth);
	struct gss_cl_ctx *ctx = gss_cred->gc_ctx;

	if (gss_destroying_context(cred))
		return;
	rcu_assign_pointer(gss_cred->gc_ctx, NULL);
	call_rcu(&cred->cr_rcu, gss_free_cred_callback);
	if (ctx)
		gss_put_ctx(ctx);
	kref_put(&gss_auth->kref, gss_free_callback);
}

/*
 * Lookup RPCSEC_GSS cred for the current process
 */
static struct rpc_cred *
gss_lookup_cred(struct rpc_auth *auth, struct auth_cred *acred, int flags)
{
	return rpcauth_lookup_credcache(auth, acred, flags);
}

static struct rpc_cred *
gss_create_cred(struct rpc_auth *auth, struct auth_cred *acred, int flags)
{
	struct gss_auth *gss_auth = container_of(auth, struct gss_auth, rpc_auth);
	struct gss_cred	*cred = NULL;
	int err = -ENOMEM;

	dprintk("RPC:       gss_create_cred for uid %d, flavor %d\n",
		acred->uid, auth->au_flavor);

	if (!(cred = kzalloc(sizeof(*cred), GFP_KERNEL)))
		goto out_err;

	rpcauth_init_cred(&cred->gc_base, acred, auth, &gss_credops);
	/*
	 * Note: in order to force a call to call_refresh(), we deliberately
	 * fail to flag the credential as RPCAUTH_CRED_UPTODATE.
	 */
	cred->gc_base.cr_flags = 1UL << RPCAUTH_CRED_NEW;
	cred->gc_service = gss_auth->service;
	kref_get(&gss_auth->kref);
	return &cred->gc_base;

out_err:
	dprintk("RPC:       gss_create_cred failed with error %d\n", err);
	return ERR_PTR(err);
}

static int
gss_cred_init(struct rpc_auth *auth, struct rpc_cred *cred)
{
	struct gss_auth *gss_auth = container_of(auth, struct gss_auth, rpc_auth);
	struct gss_cred *gss_cred = container_of(cred,struct gss_cred, gc_base);
	int err;

	do {
		err = gss_create_upcall(gss_auth, gss_cred);
	} while (err == -EAGAIN);
	return err;
}

static int
gss_match(struct auth_cred *acred, struct rpc_cred *rc, int flags)
{
	struct gss_cred *gss_cred = container_of(rc, struct gss_cred, gc_base);

	/*
	 * If the searchflags have set RPCAUTH_LOOKUP_NEW, then
	 * we don't really care if the credential has expired or not,
	 * since the caller should be prepared to reinitialise it.
	 */
	if ((flags & RPCAUTH_LOOKUP_NEW) && test_bit(RPCAUTH_CRED_NEW, &rc->cr_flags))
		goto out;
	/* Don't match with creds that have expired. */
	if (gss_cred->gc_ctx && time_after(jiffies, gss_cred->gc_ctx->gc_expiry))
		return 0;
out:
	return (rc->cr_uid == acred->uid);
}

/*
* Marshal credentials.
* Maybe we should keep a cached credential for performance reasons.
*/
static __be32 *
gss_marshal(struct rpc_task *task, __be32 *p)
{
	struct rpc_cred *cred = task->tk_msg.rpc_cred;
	struct gss_cred	*gss_cred = container_of(cred, struct gss_cred,
						 gc_base);
	struct gss_cl_ctx	*ctx = gss_cred_get_ctx(cred);
	__be32		*cred_len;
	struct rpc_rqst *req = task->tk_rqstp;
	u32             maj_stat = 0;
	struct xdr_netobj mic;
	struct kvec	iov;
	struct xdr_buf	verf_buf;

	dprintk("RPC: %5u gss_marshal\n", task->tk_pid);

	*p++ = htonl(RPC_AUTH_GSS);
	cred_len = p++;

	spin_lock(&ctx->gc_seq_lock);
	req->rq_seqno = ctx->gc_seq++;
	spin_unlock(&ctx->gc_seq_lock);

	*p++ = htonl((u32) RPC_GSS_VERSION);
	*p++ = htonl((u32) ctx->gc_proc);
	*p++ = htonl((u32) req->rq_seqno);
	*p++ = htonl((u32) gss_cred->gc_service);
	p = xdr_encode_netobj(p, &ctx->gc_wire_ctx);
	*cred_len = htonl((p - (cred_len + 1)) << 2);

	/* We compute the checksum for the verifier over the xdr-encoded bytes
	 * starting with the xid and ending at the end of the credential: */
	iov.iov_base = xprt_skip_transport_header(task->tk_xprt,
					req->rq_snd_buf.head[0].iov_base);
	iov.iov_len = (u8 *)p - (u8 *)iov.iov_base;
	xdr_buf_from_iov(&iov, &verf_buf);

	/* set verifier flavor*/
	*p++ = htonl(RPC_AUTH_GSS);

	mic.data = (u8 *)(p + 1);
	maj_stat = gss_get_mic(ctx->gc_gss_ctx, &verf_buf, &mic);
	if (maj_stat == GSS_S_CONTEXT_EXPIRED) {
		clear_bit(RPCAUTH_CRED_UPTODATE, &cred->cr_flags);
	} else if (maj_stat != 0) {
		printk("gss_marshal: gss_get_mic FAILED (%d)\n", maj_stat);
		goto out_put_ctx;
	}
	p = xdr_encode_opaque(p, NULL, mic.len);
	gss_put_ctx(ctx);
	return p;
out_put_ctx:
	gss_put_ctx(ctx);
	return NULL;
}

/*
* Refresh credentials. XXX - finish
*/
static int
gss_refresh(struct rpc_task *task)
{

	if (!gss_cred_is_uptodate_ctx(task->tk_msg.rpc_cred))
		return gss_refresh_upcall(task);
	return 0;
}

/* Dummy refresh routine: used only when destroying the context */
static int
gss_refresh_null(struct rpc_task *task)
{
	return -EACCES;
}

static __be32 *
gss_validate(struct rpc_task *task, __be32 *p)
{
	struct rpc_cred *cred = task->tk_msg.rpc_cred;
	struct gss_cl_ctx *ctx = gss_cred_get_ctx(cred);
	__be32		seq;
	struct kvec	iov;
	struct xdr_buf	verf_buf;
	struct xdr_netobj mic;
	u32		flav,len;
	u32		maj_stat;

	dprintk("RPC: %5u gss_validate\n", task->tk_pid);

	flav = ntohl(*p++);
	if ((len = ntohl(*p++)) > RPC_MAX_AUTH_SIZE)
		goto out_bad;
	if (flav != RPC_AUTH_GSS)
		goto out_bad;
	seq = htonl(task->tk_rqstp->rq_seqno);
	iov.iov_base = &seq;
	iov.iov_len = sizeof(seq);
	xdr_buf_from_iov(&iov, &verf_buf);
	mic.data = (u8 *)p;
	mic.len = len;

	maj_stat = gss_verify_mic(ctx->gc_gss_ctx, &verf_buf, &mic);
	if (maj_stat == GSS_S_CONTEXT_EXPIRED)
		clear_bit(RPCAUTH_CRED_UPTODATE, &cred->cr_flags);
	if (maj_stat) {
		dprintk("RPC: %5u gss_validate: gss_verify_mic returned "
				"error 0x%08x\n", task->tk_pid, maj_stat);
		goto out_bad;
	}
	/* We leave it to unwrap to calculate au_rslack. For now we just
	 * calculate the length of the verifier: */
	cred->cr_auth->au_verfsize = XDR_QUADLEN(len) + 2;
	gss_put_ctx(ctx);
	dprintk("RPC: %5u gss_validate: gss_verify_mic succeeded.\n",
			task->tk_pid);
	return p + XDR_QUADLEN(len);
out_bad:
	gss_put_ctx(ctx);
	dprintk("RPC: %5u gss_validate failed.\n", task->tk_pid);
	return NULL;
}

static inline int
gss_wrap_req_integ(struct rpc_cred *cred, struct gss_cl_ctx *ctx,
		kxdrproc_t encode, struct rpc_rqst *rqstp, __be32 *p, void *obj)
{
	struct xdr_buf	*snd_buf = &rqstp->rq_snd_buf;
	struct xdr_buf	integ_buf;
	__be32          *integ_len = NULL;
	struct xdr_netobj mic;
	u32		offset;
	__be32		*q;
	struct kvec	*iov;
	u32             maj_stat = 0;
	int		status = -EIO;

	integ_len = p++;
	offset = (u8 *)p - (u8 *)snd_buf->head[0].iov_base;
	*p++ = htonl(rqstp->rq_seqno);

	status = rpc_call_xdrproc(encode, rqstp, p, obj);
	if (status)
		return status;

	if (xdr_buf_subsegment(snd_buf, &integ_buf,
				offset, snd_buf->len - offset))
		return status;
	*integ_len = htonl(integ_buf.len);

	/* guess whether we're in the head or the tail: */
	if (snd_buf->page_len || snd_buf->tail[0].iov_len)
		iov = snd_buf->tail;
	else
		iov = snd_buf->head;
	p = iov->iov_base + iov->iov_len;
	mic.data = (u8 *)(p + 1);

	maj_stat = gss_get_mic(ctx->gc_gss_ctx, &integ_buf, &mic);
	status = -EIO; /* XXX? */
	if (maj_stat == GSS_S_CONTEXT_EXPIRED)
		clear_bit(RPCAUTH_CRED_UPTODATE, &cred->cr_flags);
	else if (maj_stat)
		return status;
	q = xdr_encode_opaque(p, NULL, mic.len);

	offset = (u8 *)q - (u8 *)p;
	iov->iov_len += offset;
	snd_buf->len += offset;
	return 0;
}

static void
priv_release_snd_buf(struct rpc_rqst *rqstp)
{
	int i;

	for (i=0; i < rqstp->rq_enc_pages_num; i++)
		__free_page(rqstp->rq_enc_pages[i]);
	kfree(rqstp->rq_enc_pages);
}

static int
alloc_enc_pages(struct rpc_rqst *rqstp)
{
	struct xdr_buf *snd_buf = &rqstp->rq_snd_buf;
	int first, last, i;

	if (snd_buf->page_len == 0) {
		rqstp->rq_enc_pages_num = 0;
		return 0;
	}

	first = snd_buf->page_base >> PAGE_CACHE_SHIFT;
	last = (snd_buf->page_base + snd_buf->page_len - 1) >> PAGE_CACHE_SHIFT;
	rqstp->rq_enc_pages_num = last - first + 1 + 1;
	rqstp->rq_enc_pages
		= kmalloc(rqstp->rq_enc_pages_num * sizeof(struct page *),
				GFP_NOFS);
	if (!rqstp->rq_enc_pages)
		goto out;
	for (i=0; i < rqstp->rq_enc_pages_num; i++) {
		rqstp->rq_enc_pages[i] = alloc_page(GFP_NOFS);
		if (rqstp->rq_enc_pages[i] == NULL)
			goto out_free;
	}
	rqstp->rq_release_snd_buf = priv_release_snd_buf;
	return 0;
out_free:
	for (i--; i >= 0; i--) {
		__free_page(rqstp->rq_enc_pages[i]);
	}
out:
	return -EAGAIN;
}

static inline int
gss_wrap_req_priv(struct rpc_cred *cred, struct gss_cl_ctx *ctx,
		kxdrproc_t encode, struct rpc_rqst *rqstp, __be32 *p, void *obj)
{
	struct xdr_buf	*snd_buf = &rqstp->rq_snd_buf;
	u32		offset;
	u32             maj_stat;
	int		status;
	__be32		*opaque_len;
	struct page	**inpages;
	int		first;
	int		pad;
	struct kvec	*iov;
	char		*tmp;

	opaque_len = p++;
	offset = (u8 *)p - (u8 *)snd_buf->head[0].iov_base;
	*p++ = htonl(rqstp->rq_seqno);

	status = rpc_call_xdrproc(encode, rqstp, p, obj);
	if (status)
		return status;

	status = alloc_enc_pages(rqstp);
	if (status)
		return status;
	first = snd_buf->page_base >> PAGE_CACHE_SHIFT;
	inpages = snd_buf->pages + first;
	snd_buf->pages = rqstp->rq_enc_pages;
	snd_buf->page_base -= first << PAGE_CACHE_SHIFT;
	/* Give the tail its own page, in case we need extra space in the
	 * head when wrapping: */
	if (snd_buf->page_len || snd_buf->tail[0].iov_len) {
		tmp = page_address(rqstp->rq_enc_pages[rqstp->rq_enc_pages_num - 1]);
		memcpy(tmp, snd_buf->tail[0].iov_base, snd_buf->tail[0].iov_len);
		snd_buf->tail[0].iov_base = tmp;
	}
	maj_stat = gss_wrap(ctx->gc_gss_ctx, offset, snd_buf, inpages);
	/* RPC_SLACK_SPACE should prevent this ever happening: */
	BUG_ON(snd_buf->len > snd_buf->buflen);
	status = -EIO;
	/* We're assuming that when GSS_S_CONTEXT_EXPIRED, the encryption was
	 * done anyway, so it's safe to put the request on the wire: */
	if (maj_stat == GSS_S_CONTEXT_EXPIRED)
		clear_bit(RPCAUTH_CRED_UPTODATE, &cred->cr_flags);
	else if (maj_stat)
		return status;

	*opaque_len = htonl(snd_buf->len - offset);
	/* guess whether we're in the head or the tail: */
	if (snd_buf->page_len || snd_buf->tail[0].iov_len)
		iov = snd_buf->tail;
	else
		iov = snd_buf->head;
	p = iov->iov_base + iov->iov_len;
	pad = 3 - ((snd_buf->len - offset - 1) & 3);
	memset(p, 0, pad);
	iov->iov_len += pad;
	snd_buf->len += pad;

	return 0;
}

static int
gss_wrap_req(struct rpc_task *task,
	     kxdrproc_t encode, void *rqstp, __be32 *p, void *obj)
{
	struct rpc_cred *cred = task->tk_msg.rpc_cred;
	struct gss_cred	*gss_cred = container_of(cred, struct gss_cred,
			gc_base);
	struct gss_cl_ctx *ctx = gss_cred_get_ctx(cred);
	int             status = -EIO;

	dprintk("RPC: %5u gss_wrap_req\n", task->tk_pid);
	if (ctx->gc_proc != RPC_GSS_PROC_DATA) {
		/* The spec seems a little ambiguous here, but I think that not
		 * wrapping context destruction requests makes the most sense.
		 */
		status = rpc_call_xdrproc(encode, rqstp, p, obj);
		goto out;
	}
	switch (gss_cred->gc_service) {
		case RPC_GSS_SVC_NONE:
			status = rpc_call_xdrproc(encode, rqstp, p, obj);
			break;
		case RPC_GSS_SVC_INTEGRITY:
			status = gss_wrap_req_integ(cred, ctx, encode,
								rqstp, p, obj);
			break;
		case RPC_GSS_SVC_PRIVACY:
			status = gss_wrap_req_priv(cred, ctx, encode,
					rqstp, p, obj);
			break;
	}
out:
	gss_put_ctx(ctx);
	dprintk("RPC: %5u gss_wrap_req returning %d\n", task->tk_pid, status);
	return status;
}

static inline int
gss_unwrap_resp_integ(struct rpc_cred *cred, struct gss_cl_ctx *ctx,
		struct rpc_rqst *rqstp, __be32 **p)
{
	struct xdr_buf	*rcv_buf = &rqstp->rq_rcv_buf;
	struct xdr_buf integ_buf;
	struct xdr_netobj mic;
	u32 data_offset, mic_offset;
	u32 integ_len;
	u32 maj_stat;
	int status = -EIO;

	integ_len = ntohl(*(*p)++);
	if (integ_len & 3)
		return status;
	data_offset = (u8 *)(*p) - (u8 *)rcv_buf->head[0].iov_base;
	mic_offset = integ_len + data_offset;
	if (mic_offset > rcv_buf->len)
		return status;
	if (ntohl(*(*p)++) != rqstp->rq_seqno)
		return status;

	if (xdr_buf_subsegment(rcv_buf, &integ_buf, data_offset,
				mic_offset - data_offset))
		return status;

	if (xdr_buf_read_netobj(rcv_buf, &mic, mic_offset))
		return status;

	maj_stat = gss_verify_mic(ctx->gc_gss_ctx, &integ_buf, &mic);
	if (maj_stat == GSS_S_CONTEXT_EXPIRED)
		clear_bit(RPCAUTH_CRED_UPTODATE, &cred->cr_flags);
	if (maj_stat != GSS_S_COMPLETE)
		return status;
	return 0;
}

static inline int
gss_unwrap_resp_priv(struct rpc_cred *cred, struct gss_cl_ctx *ctx,
		struct rpc_rqst *rqstp, __be32 **p)
{
	struct xdr_buf  *rcv_buf = &rqstp->rq_rcv_buf;
	u32 offset;
	u32 opaque_len;
	u32 maj_stat;
	int status = -EIO;

	opaque_len = ntohl(*(*p)++);
	offset = (u8 *)(*p) - (u8 *)rcv_buf->head[0].iov_base;
	if (offset + opaque_len > rcv_buf->len)
		return status;
	/* remove padding: */
	rcv_buf->len = offset + opaque_len;

	maj_stat = gss_unwrap(ctx->gc_gss_ctx, offset, rcv_buf);
	if (maj_stat == GSS_S_CONTEXT_EXPIRED)
		clear_bit(RPCAUTH_CRED_UPTODATE, &cred->cr_flags);
	if (maj_stat != GSS_S_COMPLETE)
		return status;
	if (ntohl(*(*p)++) != rqstp->rq_seqno)
		return status;

	return 0;
}


static int
gss_unwrap_resp(struct rpc_task *task,
		kxdrproc_t decode, void *rqstp, __be32 *p, void *obj)
{
	struct rpc_cred *cred = task->tk_msg.rpc_cred;
	struct gss_cred *gss_cred = container_of(cred, struct gss_cred,
			gc_base);
	struct gss_cl_ctx *ctx = gss_cred_get_ctx(cred);
	__be32		*savedp = p;
	struct kvec	*head = ((struct rpc_rqst *)rqstp)->rq_rcv_buf.head;
	int		savedlen = head->iov_len;
	int             status = -EIO;

	if (ctx->gc_proc != RPC_GSS_PROC_DATA)
		goto out_decode;
	switch (gss_cred->gc_service) {
		case RPC_GSS_SVC_NONE:
			break;
		case RPC_GSS_SVC_INTEGRITY:
			status = gss_unwrap_resp_integ(cred, ctx, rqstp, &p);
			if (status)
				goto out;
			break;
		case RPC_GSS_SVC_PRIVACY:
			status = gss_unwrap_resp_priv(cred, ctx, rqstp, &p);
			if (status)
				goto out;
			break;
	}
	/* take into account extra slack for integrity and privacy cases: */
	cred->cr_auth->au_rslack = cred->cr_auth->au_verfsize + (p - savedp)
						+ (savedlen - head->iov_len);
out_decode:
	status = rpc_call_xdrproc(decode, rqstp, p, obj);
out:
	gss_put_ctx(ctx);
	dprintk("RPC: %5u gss_unwrap_resp returning %d\n", task->tk_pid,
			status);
	return status;
}

static const struct rpc_authops authgss_ops = {
	.owner		= THIS_MODULE,
	.au_flavor	= RPC_AUTH_GSS,
#ifdef RPC_DEBUG
	.au_name	= "RPCSEC_GSS",
#endif
	.create		= gss_create,
	.destroy	= gss_destroy,
	.lookup_cred	= gss_lookup_cred,
	.crcreate	= gss_create_cred
};

static const struct rpc_credops gss_credops = {
	.cr_name	= "AUTH_GSS",
	.crdestroy	= gss_destroy_cred,
	.cr_init	= gss_cred_init,
	.crmatch	= gss_match,
	.crmarshal	= gss_marshal,
	.crrefresh	= gss_refresh,
	.crvalidate	= gss_validate,
	.crwrap_req	= gss_wrap_req,
	.crunwrap_resp	= gss_unwrap_resp,
};

static const struct rpc_credops gss_nullops = {
	.cr_name	= "AUTH_GSS",
	.crdestroy	= gss_destroy_cred,
	.crmatch	= gss_match,
	.crmarshal	= gss_marshal,
	.crrefresh	= gss_refresh_null,
	.crvalidate	= gss_validate,
	.crwrap_req	= gss_wrap_req,
	.crunwrap_resp	= gss_unwrap_resp,
};

static struct rpc_pipe_ops gss_upcall_ops = {
	.upcall		= gss_pipe_upcall,
	.downcall	= gss_pipe_downcall,
	.destroy_msg	= gss_pipe_destroy_msg,
	.release_pipe	= gss_pipe_release,
};

/*
 * Initialize RPCSEC_GSS module
 */
static int __init init_rpcsec_gss(void)
{
	int err = 0;

	err = rpcauth_register(&authgss_ops);
	if (err)
		goto out;
	err = gss_svc_init();
	if (err)
		goto out_unregister;
	return 0;
out_unregister:
	rpcauth_unregister(&authgss_ops);
out:
	return err;
}

static void __exit exit_rpcsec_gss(void)
{
	gss_svc_shutdown();
	rpcauth_unregister(&authgss_ops);
}

MODULE_LICENSE("GPL");
module_init(init_rpcsec_gss)
module_exit(exit_rpcsec_gss)
