/*
 * fs/nfs/idmap.c
 *
 *  UID and GID to name mapping for clients.
 *
 *  Copyright (c) 2002 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Marius Aamodt Eriksen <marius@umich.edu>
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
 */

#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/sched.h>

#include <linux/sunrpc/clnt.h>
#include <linux/workqueue.h>
#include <linux/sunrpc/rpc_pipe_fs.h>

#include <linux/nfs_fs.h>

#include <linux/nfs_idmap.h>
#include "nfs4_fs.h"

#define IDMAP_HASH_SZ          128

/* Default cache timeout is 10 minutes */
unsigned int nfs_idmap_cache_timeout = 600 * HZ;

static int param_set_idmap_timeout(const char *val, struct kernel_param *kp)
{
	char *endp;
	int num = simple_strtol(val, &endp, 0);
	int jif = num * HZ;
	if (endp == val || *endp || num < 0 || jif < num)
		return -EINVAL;
	*((int *)kp->arg) = jif;
	return 0;
}

module_param_call(idmap_cache_timeout, param_set_idmap_timeout, param_get_int,
		 &nfs_idmap_cache_timeout, 0644);

struct idmap_hashent {
	unsigned long		ih_expires;
	__u32			ih_id;
	size_t			ih_namelen;
	char			ih_name[IDMAP_NAMESZ];
};

struct idmap_hashtable {
	__u8			h_type;
	struct idmap_hashent	h_entries[IDMAP_HASH_SZ];
};

struct idmap {
	struct dentry		*idmap_dentry;
	wait_queue_head_t	idmap_wq;
	struct idmap_msg	idmap_im;
	struct mutex		idmap_lock;	/* Serializes upcalls */
	struct mutex		idmap_im_lock;	/* Protects the hashtable */
	struct idmap_hashtable	idmap_user_hash;
	struct idmap_hashtable	idmap_group_hash;
};

static ssize_t idmap_pipe_upcall(struct file *, struct rpc_pipe_msg *,
				 char __user *, size_t);
static ssize_t idmap_pipe_downcall(struct file *, const char __user *,
				   size_t);
static void idmap_pipe_destroy_msg(struct rpc_pipe_msg *);

static unsigned int fnvhash32(const void *, size_t);

static struct rpc_pipe_ops idmap_upcall_ops = {
	.upcall		= idmap_pipe_upcall,
	.downcall	= idmap_pipe_downcall,
	.destroy_msg	= idmap_pipe_destroy_msg,
};

int
nfs_idmap_new(struct nfs_client *clp)
{
	struct idmap *idmap;
	int error;

	BUG_ON(clp->cl_idmap != NULL);

	idmap = kzalloc(sizeof(*idmap), GFP_KERNEL);
	if (idmap == NULL)
		return -ENOMEM;

	idmap->idmap_dentry = rpc_mkpipe(clp->cl_rpcclient->cl_dentry, "idmap",
					 idmap, &idmap_upcall_ops, 0);
	if (IS_ERR(idmap->idmap_dentry)) {
		error = PTR_ERR(idmap->idmap_dentry);
		kfree(idmap);
		return error;
	}

	mutex_init(&idmap->idmap_lock);
	mutex_init(&idmap->idmap_im_lock);
	init_waitqueue_head(&idmap->idmap_wq);
	idmap->idmap_user_hash.h_type = IDMAP_TYPE_USER;
	idmap->idmap_group_hash.h_type = IDMAP_TYPE_GROUP;

	clp->cl_idmap = idmap;
	return 0;
}

void
nfs_idmap_delete(struct nfs_client *clp)
{
	struct idmap *idmap = clp->cl_idmap;

	if (!idmap)
		return;
	rpc_unlink(idmap->idmap_dentry);
	clp->cl_idmap = NULL;
	kfree(idmap);
}

/*
 * Helper routines for manipulating the hashtable
 */
static inline struct idmap_hashent *
idmap_name_hash(struct idmap_hashtable* h, const char *name, size_t len)
{
	return &h->h_entries[fnvhash32(name, len) % IDMAP_HASH_SZ];
}

static struct idmap_hashent *
idmap_lookup_name(struct idmap_hashtable *h, const char *name, size_t len)
{
	struct idmap_hashent *he = idmap_name_hash(h, name, len);

	if (he->ih_namelen != len || memcmp(he->ih_name, name, len) != 0)
		return NULL;
	if (time_after(jiffies, he->ih_expires))
		return NULL;
	return he;
}

static inline struct idmap_hashent *
idmap_id_hash(struct idmap_hashtable* h, __u32 id)
{
	return &h->h_entries[fnvhash32(&id, sizeof(id)) % IDMAP_HASH_SZ];
}

static struct idmap_hashent *
idmap_lookup_id(struct idmap_hashtable *h, __u32 id)
{
	struct idmap_hashent *he = idmap_id_hash(h, id);
	if (he->ih_id != id || he->ih_namelen == 0)
		return NULL;
	if (time_after(jiffies, he->ih_expires))
		return NULL;
	return he;
}

/*
 * Routines for allocating new entries in the hashtable.
 * For now, we just have 1 entry per bucket, so it's all
 * pretty trivial.
 */
static inline struct idmap_hashent *
idmap_alloc_name(struct idmap_hashtable *h, char *name, size_t len)
{
	return idmap_name_hash(h, name, len);
}

static inline struct idmap_hashent *
idmap_alloc_id(struct idmap_hashtable *h, __u32 id)
{
	return idmap_id_hash(h, id);
}

static void
idmap_update_entry(struct idmap_hashent *he, const char *name,
		size_t namelen, __u32 id)
{
	he->ih_id = id;
	memcpy(he->ih_name, name, namelen);
	he->ih_name[namelen] = '\0';
	he->ih_namelen = namelen;
	he->ih_expires = jiffies + nfs_idmap_cache_timeout;
}

/*
 * Name -> ID
 */
static int
nfs_idmap_id(struct idmap *idmap, struct idmap_hashtable *h,
		const char *name, size_t namelen, __u32 *id)
{
	struct rpc_pipe_msg msg;
	struct idmap_msg *im;
	struct idmap_hashent *he;
	DECLARE_WAITQUEUE(wq, current);
	int ret = -EIO;

	im = &idmap->idmap_im;

	/*
	 * String sanity checks
	 * Note that the userland daemon expects NUL terminated strings
	 */
	for (;;) {
		if (namelen == 0)
			return -EINVAL;
		if (name[namelen-1] != '\0')
			break;
		namelen--;
	}
	if (namelen >= IDMAP_NAMESZ)
		return -EINVAL;

	mutex_lock(&idmap->idmap_lock);
	mutex_lock(&idmap->idmap_im_lock);

	he = idmap_lookup_name(h, name, namelen);
	if (he != NULL) {
		*id = he->ih_id;
		ret = 0;
		goto out;
	}

	memset(im, 0, sizeof(*im));
	memcpy(im->im_name, name, namelen);

	im->im_type = h->h_type;
	im->im_conv = IDMAP_CONV_NAMETOID;

	memset(&msg, 0, sizeof(msg));
	msg.data = im;
	msg.len = sizeof(*im);

	add_wait_queue(&idmap->idmap_wq, &wq);
	if (rpc_queue_upcall(idmap->idmap_dentry->d_inode, &msg) < 0) {
		remove_wait_queue(&idmap->idmap_wq, &wq);
		goto out;
	}

	set_current_state(TASK_UNINTERRUPTIBLE);
	mutex_unlock(&idmap->idmap_im_lock);
	schedule();
	__set_current_state(TASK_RUNNING);
	remove_wait_queue(&idmap->idmap_wq, &wq);
	mutex_lock(&idmap->idmap_im_lock);

	if (im->im_status & IDMAP_STATUS_SUCCESS) {
		*id = im->im_id;
		ret = 0;
	}

 out:
	memset(im, 0, sizeof(*im));
	mutex_unlock(&idmap->idmap_im_lock);
	mutex_unlock(&idmap->idmap_lock);
	return ret;
}

/*
 * ID -> Name
 */
static int
nfs_idmap_name(struct idmap *idmap, struct idmap_hashtable *h,
		__u32 id, char *name)
{
	struct rpc_pipe_msg msg;
	struct idmap_msg *im;
	struct idmap_hashent *he;
	DECLARE_WAITQUEUE(wq, current);
	int ret = -EIO;
	unsigned int len;

	im = &idmap->idmap_im;

	mutex_lock(&idmap->idmap_lock);
	mutex_lock(&idmap->idmap_im_lock);

	he = idmap_lookup_id(h, id);
	if (he != 0) {
		memcpy(name, he->ih_name, he->ih_namelen);
		ret = he->ih_namelen;
		goto out;
	}

	memset(im, 0, sizeof(*im));
	im->im_type = h->h_type;
	im->im_conv = IDMAP_CONV_IDTONAME;
	im->im_id = id;

	memset(&msg, 0, sizeof(msg));
	msg.data = im;
	msg.len = sizeof(*im);

	add_wait_queue(&idmap->idmap_wq, &wq);

	if (rpc_queue_upcall(idmap->idmap_dentry->d_inode, &msg) < 0) {
		remove_wait_queue(&idmap->idmap_wq, &wq);
		goto out;
	}

	set_current_state(TASK_UNINTERRUPTIBLE);
	mutex_unlock(&idmap->idmap_im_lock);
	schedule();
	__set_current_state(TASK_RUNNING);
	remove_wait_queue(&idmap->idmap_wq, &wq);
	mutex_lock(&idmap->idmap_im_lock);

	if (im->im_status & IDMAP_STATUS_SUCCESS) {
		if ((len = strnlen(im->im_name, IDMAP_NAMESZ)) == 0)
			goto out;
		memcpy(name, im->im_name, len);
		ret = len;
	}

 out:
	memset(im, 0, sizeof(*im));
	mutex_unlock(&idmap->idmap_im_lock);
	mutex_unlock(&idmap->idmap_lock);
	return ret;
}

/* RPC pipefs upcall/downcall routines */
static ssize_t
idmap_pipe_upcall(struct file *filp, struct rpc_pipe_msg *msg,
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

static ssize_t
idmap_pipe_downcall(struct file *filp, const char __user *src, size_t mlen)
{
	struct rpc_inode *rpci = RPC_I(filp->f_path.dentry->d_inode);
	struct idmap *idmap = (struct idmap *)rpci->private;
	struct idmap_msg im_in, *im = &idmap->idmap_im;
	struct idmap_hashtable *h;
	struct idmap_hashent *he = NULL;
	size_t namelen_in;
	int ret;

	if (mlen != sizeof(im_in))
		return -ENOSPC;

	if (copy_from_user(&im_in, src, mlen) != 0)
		return -EFAULT;

	mutex_lock(&idmap->idmap_im_lock);

	ret = mlen;
	im->im_status = im_in.im_status;
	/* If we got an error, terminate now, and wake up pending upcalls */
	if (!(im_in.im_status & IDMAP_STATUS_SUCCESS)) {
		wake_up(&idmap->idmap_wq);
		goto out;
	}

	/* Sanity checking of strings */
	ret = -EINVAL;
	namelen_in = strnlen(im_in.im_name, IDMAP_NAMESZ);
	if (namelen_in == 0 || namelen_in == IDMAP_NAMESZ)
		goto out;

	switch (im_in.im_type) {
		case IDMAP_TYPE_USER:
			h = &idmap->idmap_user_hash;
			break;
		case IDMAP_TYPE_GROUP:
			h = &idmap->idmap_group_hash;
			break;
		default:
			goto out;
	}

	switch (im_in.im_conv) {
	case IDMAP_CONV_IDTONAME:
		/* Did we match the current upcall? */
		if (im->im_conv == IDMAP_CONV_IDTONAME
				&& im->im_type == im_in.im_type
				&& im->im_id == im_in.im_id) {
			/* Yes: copy string, including the terminating '\0'  */
			memcpy(im->im_name, im_in.im_name, namelen_in);
			im->im_name[namelen_in] = '\0';
			wake_up(&idmap->idmap_wq);
		}
		he = idmap_alloc_id(h, im_in.im_id);
		break;
	case IDMAP_CONV_NAMETOID:
		/* Did we match the current upcall? */
		if (im->im_conv == IDMAP_CONV_NAMETOID
				&& im->im_type == im_in.im_type
				&& strnlen(im->im_name, IDMAP_NAMESZ) == namelen_in
				&& memcmp(im->im_name, im_in.im_name, namelen_in) == 0) {
			im->im_id = im_in.im_id;
			wake_up(&idmap->idmap_wq);
		}
		he = idmap_alloc_name(h, im_in.im_name, namelen_in);
		break;
	default:
		goto out;
	}

	/* If the entry is valid, also copy it to the cache */
	if (he != NULL)
		idmap_update_entry(he, im_in.im_name, namelen_in, im_in.im_id);
	ret = mlen;
out:
	mutex_unlock(&idmap->idmap_im_lock);
	return ret;
}

static void
idmap_pipe_destroy_msg(struct rpc_pipe_msg *msg)
{
	struct idmap_msg *im = msg->data;
	struct idmap *idmap = container_of(im, struct idmap, idmap_im); 

	if (msg->errno >= 0)
		return;
	mutex_lock(&idmap->idmap_im_lock);
	im->im_status = IDMAP_STATUS_LOOKUPFAIL;
	wake_up(&idmap->idmap_wq);
	mutex_unlock(&idmap->idmap_im_lock);
}

/* 
 * Fowler/Noll/Vo hash
 *    http://www.isthe.com/chongo/tech/comp/fnv/
 */

#define FNV_P_32 ((unsigned int)0x01000193) /* 16777619 */
#define FNV_1_32 ((unsigned int)0x811c9dc5) /* 2166136261 */

static unsigned int fnvhash32(const void *buf, size_t buflen)
{
	const unsigned char *p, *end = (const unsigned char *)buf + buflen;
	unsigned int hash = FNV_1_32;

	for (p = buf; p < end; p++) {
		hash *= FNV_P_32;
		hash ^= (unsigned int)*p;
	}

	return hash;
}

int nfs_map_name_to_uid(struct nfs_client *clp, const char *name, size_t namelen, __u32 *uid)
{
	struct idmap *idmap = clp->cl_idmap;

	return nfs_idmap_id(idmap, &idmap->idmap_user_hash, name, namelen, uid);
}

int nfs_map_group_to_gid(struct nfs_client *clp, const char *name, size_t namelen, __u32 *uid)
{
	struct idmap *idmap = clp->cl_idmap;

	return nfs_idmap_id(idmap, &idmap->idmap_group_hash, name, namelen, uid);
}

int nfs_map_uid_to_name(struct nfs_client *clp, __u32 uid, char *buf)
{
	struct idmap *idmap = clp->cl_idmap;

	return nfs_idmap_name(idmap, &idmap->idmap_user_hash, uid, buf);
}
int nfs_map_gid_to_group(struct nfs_client *clp, __u32 uid, char *buf)
{
	struct idmap *idmap = clp->cl_idmap;

	return nfs_idmap_name(idmap, &idmap->idmap_group_hash, uid, buf);
}

