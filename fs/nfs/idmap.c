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
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/nfs_idmap.h>
#include <linux/nfs_fs.h>
#include <linux/cred.h>
#include <linux/sunrpc/sched.h>
#include <linux/nfs4.h>
#include <linux/nfs_fs_sb.h>
#include <linux/keyctl.h>
#include <linux/key-type.h>
#include <linux/rcupdate.h>
#include <linux/err.h>
#include <keys/user-type.h>

/* include files needed by legacy idmapper */
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/init.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/sched.h>
#include <linux/sunrpc/clnt.h>
#include <linux/workqueue.h>
#include <linux/sunrpc/rpc_pipe_fs.h>
#include <linux/nfs_fs.h>
#include "nfs4_fs.h"
#include "internal.h"
#include "netns.h"

#define NFS_UINT_MAXLEN 11
#define IDMAP_HASH_SZ          128

/* Default cache timeout is 10 minutes */
unsigned int nfs_idmap_cache_timeout = 600 * HZ;
const struct cred *id_resolver_cache;


/**
 * nfs_fattr_init_names - initialise the nfs_fattr owner_name/group_name fields
 * @fattr: fully initialised struct nfs_fattr
 * @owner_name: owner name string cache
 * @group_name: group name string cache
 */
void nfs_fattr_init_names(struct nfs_fattr *fattr,
		struct nfs4_string *owner_name,
		struct nfs4_string *group_name)
{
	fattr->owner_name = owner_name;
	fattr->group_name = group_name;
}

static void nfs_fattr_free_owner_name(struct nfs_fattr *fattr)
{
	fattr->valid &= ~NFS_ATTR_FATTR_OWNER_NAME;
	kfree(fattr->owner_name->data);
}

static void nfs_fattr_free_group_name(struct nfs_fattr *fattr)
{
	fattr->valid &= ~NFS_ATTR_FATTR_GROUP_NAME;
	kfree(fattr->group_name->data);
}

static bool nfs_fattr_map_owner_name(struct nfs_server *server, struct nfs_fattr *fattr)
{
	struct nfs4_string *owner = fattr->owner_name;
	__u32 uid;

	if (!(fattr->valid & NFS_ATTR_FATTR_OWNER_NAME))
		return false;
	if (nfs_map_name_to_uid(server, owner->data, owner->len, &uid) == 0) {
		fattr->uid = uid;
		fattr->valid |= NFS_ATTR_FATTR_OWNER;
	}
	return true;
}

static bool nfs_fattr_map_group_name(struct nfs_server *server, struct nfs_fattr *fattr)
{
	struct nfs4_string *group = fattr->group_name;
	__u32 gid;

	if (!(fattr->valid & NFS_ATTR_FATTR_GROUP_NAME))
		return false;
	if (nfs_map_group_to_gid(server, group->data, group->len, &gid) == 0) {
		fattr->gid = gid;
		fattr->valid |= NFS_ATTR_FATTR_GROUP;
	}
	return true;
}

/**
 * nfs_fattr_free_names - free up the NFSv4 owner and group strings
 * @fattr: a fully initialised nfs_fattr structure
 */
void nfs_fattr_free_names(struct nfs_fattr *fattr)
{
	if (fattr->valid & NFS_ATTR_FATTR_OWNER_NAME)
		nfs_fattr_free_owner_name(fattr);
	if (fattr->valid & NFS_ATTR_FATTR_GROUP_NAME)
		nfs_fattr_free_group_name(fattr);
}

/**
 * nfs_fattr_map_and_free_names - map owner/group strings into uid/gid and free
 * @server: pointer to the filesystem nfs_server structure
 * @fattr: a fully initialised nfs_fattr structure
 *
 * This helper maps the cached NFSv4 owner/group strings in fattr into
 * their numeric uid/gid equivalents, and then frees the cached strings.
 */
void nfs_fattr_map_and_free_names(struct nfs_server *server, struct nfs_fattr *fattr)
{
	if (nfs_fattr_map_owner_name(server, fattr))
		nfs_fattr_free_owner_name(fattr);
	if (nfs_fattr_map_group_name(server, fattr))
		nfs_fattr_free_group_name(fattr);
}

static int nfs_map_string_to_numeric(const char *name, size_t namelen, __u32 *res)
{
	unsigned long val;
	char buf[16];

	if (memchr(name, '@', namelen) != NULL || namelen >= sizeof(buf))
		return 0;
	memcpy(buf, name, namelen);
	buf[namelen] = '\0';
	if (strict_strtoul(buf, 0, &val) != 0)
		return 0;
	*res = val;
	return 1;
}

static int nfs_map_numeric_to_string(__u32 id, char *buf, size_t buflen)
{
	return snprintf(buf, buflen, "%u", id);
}

struct key_type key_type_id_resolver = {
	.name		= "id_resolver",
	.instantiate	= user_instantiate,
	.match		= user_match,
	.revoke		= user_revoke,
	.destroy	= user_destroy,
	.describe	= user_describe,
	.read		= user_read,
};

static int nfs_idmap_init_keyring(void)
{
	struct cred *cred;
	struct key *keyring;
	int ret = 0;

	printk(KERN_NOTICE "NFS: Registering the %s key type\n",
		key_type_id_resolver.name);

	cred = prepare_kernel_cred(NULL);
	if (!cred)
		return -ENOMEM;

	keyring = key_alloc(&key_type_keyring, ".id_resolver", 0, 0, cred,
			     (KEY_POS_ALL & ~KEY_POS_SETATTR) |
			     KEY_USR_VIEW | KEY_USR_READ,
			     KEY_ALLOC_NOT_IN_QUOTA);
	if (IS_ERR(keyring)) {
		ret = PTR_ERR(keyring);
		goto failed_put_cred;
	}

	ret = key_instantiate_and_link(keyring, NULL, 0, NULL, NULL);
	if (ret < 0)
		goto failed_put_key;

	ret = register_key_type(&key_type_id_resolver);
	if (ret < 0)
		goto failed_put_key;

	cred->thread_keyring = keyring;
	cred->jit_keyring = KEY_REQKEY_DEFL_THREAD_KEYRING;
	id_resolver_cache = cred;
	return 0;

failed_put_key:
	key_put(keyring);
failed_put_cred:
	put_cred(cred);
	return ret;
}

static void nfs_idmap_quit_keyring(void)
{
	key_revoke(id_resolver_cache->thread_keyring);
	unregister_key_type(&key_type_id_resolver);
	put_cred(id_resolver_cache);
}

/*
 * Assemble the description to pass to request_key()
 * This function will allocate a new string and update dest to point
 * at it.  The caller is responsible for freeing dest.
 *
 * On error 0 is returned.  Otherwise, the length of dest is returned.
 */
static ssize_t nfs_idmap_get_desc(const char *name, size_t namelen,
				const char *type, size_t typelen, char **desc)
{
	char *cp;
	size_t desclen = typelen + namelen + 2;

	*desc = kmalloc(desclen, GFP_KERNEL);
	if (!*desc)
		return -ENOMEM;

	cp = *desc;
	memcpy(cp, type, typelen);
	cp += typelen;
	*cp++ = ':';

	memcpy(cp, name, namelen);
	cp += namelen;
	*cp = '\0';
	return desclen;
}

static ssize_t nfs_idmap_request_key(const char *name, size_t namelen,
		const char *type, void *data, size_t data_size)
{
	const struct cred *saved_cred;
	struct key *rkey;
	char *desc;
	struct user_key_payload *payload;
	ssize_t ret;

	ret = nfs_idmap_get_desc(name, namelen, type, strlen(type), &desc);
	if (ret <= 0)
		goto out;

	saved_cred = override_creds(id_resolver_cache);
	rkey = request_key(&key_type_id_resolver, desc, "");
	revert_creds(saved_cred);
	kfree(desc);
	if (IS_ERR(rkey)) {
		ret = PTR_ERR(rkey);
		goto out;
	}

	rcu_read_lock();
	rkey->perm |= KEY_USR_VIEW;

	ret = key_validate(rkey);
	if (ret < 0)
		goto out_up;

	payload = rcu_dereference(rkey->payload.data);
	if (IS_ERR_OR_NULL(payload)) {
		ret = PTR_ERR(payload);
		goto out_up;
	}

	ret = payload->datalen;
	if (ret > 0 && ret <= data_size)
		memcpy(data, payload->data, ret);
	else
		ret = -EINVAL;

out_up:
	rcu_read_unlock();
	key_put(rkey);
out:
	return ret;
}


/* ID -> Name */
static ssize_t nfs_idmap_lookup_name(__u32 id, const char *type, char *buf, size_t buflen)
{
	char id_str[NFS_UINT_MAXLEN];
	int id_len;
	ssize_t ret;

	id_len = snprintf(id_str, sizeof(id_str), "%u", id);
	ret = nfs_idmap_request_key(id_str, id_len, type, buf, buflen);
	if (ret < 0)
		return -EINVAL;
	return ret;
}

/* Name -> ID */
static int nfs_idmap_lookup_id(const char *name, size_t namelen,
				const char *type, __u32 *id)
{
	char id_str[NFS_UINT_MAXLEN];
	long id_long;
	ssize_t data_size;
	int ret = 0;

	data_size = nfs_idmap_request_key(name, namelen, type, id_str, NFS_UINT_MAXLEN);
	if (data_size <= 0) {
		ret = -EINVAL;
	} else {
		ret = strict_strtol(id_str, 10, &id_long);
		*id = (__u32)id_long;
	}
	return ret;
}

/* idmap classic begins here */
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
	const char		*ih_name;
};

struct idmap_hashtable {
	__u8			h_type;
	struct idmap_hashent	*h_entries;
};

struct idmap {
	struct rpc_pipe		*idmap_pipe;
	wait_queue_head_t	idmap_wq;
	struct idmap_msg	idmap_im;
	struct mutex		idmap_lock;	/* Serializes upcalls */
	struct mutex		idmap_im_lock;	/* Protects the hashtable */
	struct idmap_hashtable	idmap_user_hash;
	struct idmap_hashtable	idmap_group_hash;
};

static ssize_t idmap_pipe_downcall(struct file *, const char __user *,
				   size_t);
static void idmap_pipe_destroy_msg(struct rpc_pipe_msg *);

static unsigned int fnvhash32(const void *, size_t);

static const struct rpc_pipe_ops idmap_upcall_ops = {
	.upcall		= rpc_pipe_generic_upcall,
	.downcall	= idmap_pipe_downcall,
	.destroy_msg	= idmap_pipe_destroy_msg,
};

static void __nfs_idmap_unregister(struct rpc_pipe *pipe)
{
	if (pipe->dentry)
		rpc_unlink(pipe->dentry);
}

static int __nfs_idmap_register(struct dentry *dir,
				     struct idmap *idmap,
				     struct rpc_pipe *pipe)
{
	struct dentry *dentry;

	dentry = rpc_mkpipe_dentry(dir, "idmap", idmap, pipe);
	if (IS_ERR(dentry))
		return PTR_ERR(dentry);
	pipe->dentry = dentry;
	return 0;
}

static void nfs_idmap_unregister(struct nfs_client *clp,
				      struct rpc_pipe *pipe)
{
	struct net *net = clp->net;
	struct super_block *pipefs_sb;

	pipefs_sb = rpc_get_sb_net(net);
	if (pipefs_sb) {
		__nfs_idmap_unregister(pipe);
		rpc_put_sb_net(net);
	}
}

static int nfs_idmap_register(struct nfs_client *clp,
				   struct idmap *idmap,
				   struct rpc_pipe *pipe)
{
	struct net *net = clp->net;
	struct super_block *pipefs_sb;
	int err = 0;

	pipefs_sb = rpc_get_sb_net(net);
	if (pipefs_sb) {
		if (clp->cl_rpcclient->cl_dentry)
			err = __nfs_idmap_register(clp->cl_rpcclient->cl_dentry,
						   idmap, pipe);
		rpc_put_sb_net(net);
	}
	return err;
}

int
nfs_idmap_new(struct nfs_client *clp)
{
	struct idmap *idmap;
	struct rpc_pipe *pipe;
	int error;

	BUG_ON(clp->cl_idmap != NULL);

	idmap = kzalloc(sizeof(*idmap), GFP_KERNEL);
	if (idmap == NULL)
		return -ENOMEM;

	pipe = rpc_mkpipe_data(&idmap_upcall_ops, 0);
	if (IS_ERR(pipe)) {
		error = PTR_ERR(pipe);
		kfree(idmap);
		return error;
	}
	error = nfs_idmap_register(clp, idmap, pipe);
	if (error) {
		rpc_destroy_pipe_data(pipe);
		kfree(idmap);
		return error;
	}
	idmap->idmap_pipe = pipe;
	mutex_init(&idmap->idmap_lock);
	mutex_init(&idmap->idmap_im_lock);
	init_waitqueue_head(&idmap->idmap_wq);
	idmap->idmap_user_hash.h_type = IDMAP_TYPE_USER;
	idmap->idmap_group_hash.h_type = IDMAP_TYPE_GROUP;

	clp->cl_idmap = idmap;
	return 0;
}

static void
idmap_alloc_hashtable(struct idmap_hashtable *h)
{
	if (h->h_entries != NULL)
		return;
	h->h_entries = kcalloc(IDMAP_HASH_SZ,
			sizeof(*h->h_entries),
			GFP_KERNEL);
}

static void
idmap_free_hashtable(struct idmap_hashtable *h)
{
	int i;

	if (h->h_entries == NULL)
		return;
	for (i = 0; i < IDMAP_HASH_SZ; i++)
		kfree(h->h_entries[i].ih_name);
	kfree(h->h_entries);
}

void
nfs_idmap_delete(struct nfs_client *clp)
{
	struct idmap *idmap = clp->cl_idmap;

	if (!idmap)
		return;
	nfs_idmap_unregister(clp, idmap->idmap_pipe);
	rpc_destroy_pipe_data(idmap->idmap_pipe);
	clp->cl_idmap = NULL;
	idmap_free_hashtable(&idmap->idmap_user_hash);
	idmap_free_hashtable(&idmap->idmap_group_hash);
	kfree(idmap);
}

static int __rpc_pipefs_event(struct nfs_client *clp, unsigned long event,
			      struct super_block *sb)
{
	int err = 0;

	switch (event) {
	case RPC_PIPEFS_MOUNT:
		BUG_ON(clp->cl_rpcclient->cl_dentry == NULL);
		err = __nfs_idmap_register(clp->cl_rpcclient->cl_dentry,
						clp->cl_idmap,
						clp->cl_idmap->idmap_pipe);
		break;
	case RPC_PIPEFS_UMOUNT:
		if (clp->cl_idmap->idmap_pipe) {
			struct dentry *parent;

			parent = clp->cl_idmap->idmap_pipe->dentry->d_parent;
			__nfs_idmap_unregister(clp->cl_idmap->idmap_pipe);
			/*
			 * Note: This is a dirty hack. SUNRPC hook has been
			 * called already but simple_rmdir() call for the
			 * directory returned with error because of idmap pipe
			 * inside. Thus now we have to remove this directory
			 * here.
			 */
			if (rpc_rmdir(parent))
				printk(KERN_ERR "NFS: %s: failed to remove "
					"clnt dir!\n", __func__);
		}
		break;
	default:
		printk(KERN_ERR "NFS: %s: unknown event: %ld\n", __func__,
			event);
		return -ENOTSUPP;
	}
	return err;
}

static int rpc_pipefs_event(struct notifier_block *nb, unsigned long event,
			    void *ptr)
{
	struct super_block *sb = ptr;
	struct nfs_net *nn = net_generic(sb->s_fs_info, nfs_net_id);
	struct nfs_client *clp;
	int error = 0;

	spin_lock(&nn->nfs_client_lock);
	list_for_each_entry(clp, &nn->nfs_client_list, cl_share_link) {
		if (clp->rpc_ops != &nfs_v4_clientops)
			continue;
		error = __rpc_pipefs_event(clp, event, sb);
		if (error)
			break;
	}
	spin_unlock(&nn->nfs_client_lock);
	return error;
}

#define PIPEFS_NFS_PRIO		1

static struct notifier_block nfs_idmap_block = {
	.notifier_call	= rpc_pipefs_event,
	.priority	= SUNRPC_PIPEFS_NFS_PRIO,
};

int nfs_idmap_init(void)
{
	int ret;
	ret = nfs_idmap_init_keyring();
	if (ret != 0)
		goto out;
	ret = rpc_pipefs_notifier_register(&nfs_idmap_block);
	if (ret != 0)
		nfs_idmap_quit_keyring();
out:
	return ret;
}

void nfs_idmap_quit(void)
{
	rpc_pipefs_notifier_unregister(&nfs_idmap_block);
	nfs_idmap_quit_keyring();
}

/*
 * Helper routines for manipulating the hashtable
 */
static inline struct idmap_hashent *
idmap_name_hash(struct idmap_hashtable* h, const char *name, size_t len)
{
	if (h->h_entries == NULL)
		return NULL;
	return &h->h_entries[fnvhash32(name, len) % IDMAP_HASH_SZ];
}

static struct idmap_hashent *
idmap_lookup_name(struct idmap_hashtable *h, const char *name, size_t len)
{
	struct idmap_hashent *he = idmap_name_hash(h, name, len);

	if (he == NULL)
		return NULL;
	if (he->ih_namelen != len || memcmp(he->ih_name, name, len) != 0)
		return NULL;
	if (time_after(jiffies, he->ih_expires))
		return NULL;
	return he;
}

static inline struct idmap_hashent *
idmap_id_hash(struct idmap_hashtable* h, __u32 id)
{
	if (h->h_entries == NULL)
		return NULL;
	return &h->h_entries[fnvhash32(&id, sizeof(id)) % IDMAP_HASH_SZ];
}

static struct idmap_hashent *
idmap_lookup_id(struct idmap_hashtable *h, __u32 id)
{
	struct idmap_hashent *he = idmap_id_hash(h, id);

	if (he == NULL)
		return NULL;
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
	idmap_alloc_hashtable(h);
	return idmap_name_hash(h, name, len);
}

static inline struct idmap_hashent *
idmap_alloc_id(struct idmap_hashtable *h, __u32 id)
{
	idmap_alloc_hashtable(h);
	return idmap_id_hash(h, id);
}

static void
idmap_update_entry(struct idmap_hashent *he, const char *name,
		size_t namelen, __u32 id)
{
	char *str = kmalloc(namelen + 1, GFP_KERNEL);
	if (str == NULL)
		return;
	kfree(he->ih_name);
	he->ih_id = id;
	memcpy(str, name, namelen);
	str[namelen] = '\0';
	he->ih_name = str;
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
	if (rpc_queue_upcall(idmap->idmap_pipe, &msg) < 0) {
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
	if (he) {
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

	if (rpc_queue_upcall(idmap->idmap_pipe, &msg) < 0) {
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

int nfs_map_name_to_uid(const struct nfs_server *server, const char *name, size_t namelen, __u32 *uid)
{
	struct idmap *idmap = server->nfs_client->cl_idmap;
	int ret = -EINVAL;

	if (nfs_map_string_to_numeric(name, namelen, uid))
		return 0;
	ret = nfs_idmap_lookup_id(name, namelen, "uid", uid);
	if (ret < 0)
		ret = nfs_idmap_id(idmap, &idmap->idmap_user_hash, name, namelen, uid);
	return ret;
}

int nfs_map_group_to_gid(const struct nfs_server *server, const char *name, size_t namelen, __u32 *gid)
{
	struct idmap *idmap = server->nfs_client->cl_idmap;
	int ret = -EINVAL;

	if (nfs_map_string_to_numeric(name, namelen, gid))
		return 0;
	ret = nfs_idmap_lookup_id(name, namelen, "gid", gid);
	if (ret < 0)
		ret = nfs_idmap_id(idmap, &idmap->idmap_group_hash, name, namelen, gid);
	return ret;
}

int nfs_map_uid_to_name(const struct nfs_server *server, __u32 uid, char *buf, size_t buflen)
{
	struct idmap *idmap = server->nfs_client->cl_idmap;
	int ret = -EINVAL;

	if (!(server->caps & NFS_CAP_UIDGID_NOMAP)) {
		ret = nfs_idmap_lookup_name(uid, "user", buf, buflen);
		if (ret < 0)
			ret = nfs_idmap_name(idmap, &idmap->idmap_user_hash, uid, buf);
	}
	if (ret < 0)
		ret = nfs_map_numeric_to_string(uid, buf, buflen);
	return ret;
}
int nfs_map_gid_to_group(const struct nfs_server *server, __u32 gid, char *buf, size_t buflen)
{
	struct idmap *idmap = server->nfs_client->cl_idmap;
	int ret = -EINVAL;

	if (!(server->caps & NFS_CAP_UIDGID_NOMAP)) {
		ret = nfs_idmap_lookup_name(gid, "group", buf, buflen);
		if (ret < 0)
			ret = nfs_idmap_name(idmap, &idmap->idmap_group_hash, gid, buf);
	}
	if (ret < 0)
		ret = nfs_map_numeric_to_string(gid, buf, buflen);
	return ret;
}
