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
#include <linux/parser.h>
#include <linux/fs.h>
#include <linux/nfs_idmap.h>
#include <net/net_namespace.h>
#include <linux/sunrpc/rpc_pipe_fs.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_fs_sb.h>
#include <linux/key.h>
#include <linux/keyctl.h>
#include <linux/key-type.h>
#include <keys/user-type.h>
#include <linux/module.h>

#include "internal.h"
#include "netns.h"

#define NFS_UINT_MAXLEN 11

/* Default cache timeout is 10 minutes */
unsigned int nfs_idmap_cache_timeout = 600;
static const struct cred *id_resolver_cache;
static struct key_type key_type_id_resolver_legacy;

struct idmap {
	struct rpc_pipe		*idmap_pipe;
	struct key_construction	*idmap_key_cons;
	struct mutex		idmap_mutex;
};

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

static struct key_type key_type_id_resolver = {
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

	set_bit(KEY_FLAG_ROOT_CAN_CLEAR, &keyring->flags);
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

static ssize_t nfs_idmap_request_key(struct key_type *key_type,
				     const char *name, size_t namelen,
				     const char *type, void *data,
				     size_t data_size, struct idmap *idmap)
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
	if (idmap)
		rkey = request_key_with_auxdata(key_type, desc, "", 0, idmap);
	else
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

static ssize_t nfs_idmap_get_key(const char *name, size_t namelen,
				 const char *type, void *data,
				 size_t data_size, struct idmap *idmap)
{
	ssize_t ret = nfs_idmap_request_key(&key_type_id_resolver,
					    name, namelen, type, data,
					    data_size, NULL);
	if (ret < 0) {
		mutex_lock(&idmap->idmap_mutex);
		ret = nfs_idmap_request_key(&key_type_id_resolver_legacy,
					    name, namelen, type, data,
					    data_size, idmap);
		mutex_unlock(&idmap->idmap_mutex);
	}
	return ret;
}

/* ID -> Name */
static ssize_t nfs_idmap_lookup_name(__u32 id, const char *type, char *buf,
				     size_t buflen, struct idmap *idmap)
{
	char id_str[NFS_UINT_MAXLEN];
	int id_len;
	ssize_t ret;

	id_len = snprintf(id_str, sizeof(id_str), "%u", id);
	ret = nfs_idmap_get_key(id_str, id_len, type, buf, buflen, idmap);
	if (ret < 0)
		return -EINVAL;
	return ret;
}

/* Name -> ID */
static int nfs_idmap_lookup_id(const char *name, size_t namelen, const char *type,
			       __u32 *id, struct idmap *idmap)
{
	char id_str[NFS_UINT_MAXLEN];
	long id_long;
	ssize_t data_size;
	int ret = 0;

	data_size = nfs_idmap_get_key(name, namelen, type, id_str, NFS_UINT_MAXLEN, idmap);
	if (data_size <= 0) {
		ret = -EINVAL;
	} else {
		ret = strict_strtol(id_str, 10, &id_long);
		*id = (__u32)id_long;
	}
	return ret;
}

/* idmap classic begins here */
module_param(nfs_idmap_cache_timeout, int, 0644);

enum {
	Opt_find_uid, Opt_find_gid, Opt_find_user, Opt_find_group, Opt_find_err
};

static const match_table_t nfs_idmap_tokens = {
	{ Opt_find_uid, "uid:%s" },
	{ Opt_find_gid, "gid:%s" },
	{ Opt_find_user, "user:%s" },
	{ Opt_find_group, "group:%s" },
	{ Opt_find_err, NULL }
};

static int nfs_idmap_legacy_upcall(struct key_construction *, const char *, void *);
static ssize_t idmap_pipe_downcall(struct file *, const char __user *,
				   size_t);
static void idmap_pipe_destroy_msg(struct rpc_pipe_msg *);

static const struct rpc_pipe_ops idmap_upcall_ops = {
	.upcall		= rpc_pipe_generic_upcall,
	.downcall	= idmap_pipe_downcall,
	.destroy_msg	= idmap_pipe_destroy_msg,
};

static struct key_type key_type_id_resolver_legacy = {
	.name		= "id_resolver",
	.instantiate	= user_instantiate,
	.match		= user_match,
	.revoke		= user_revoke,
	.destroy	= user_destroy,
	.describe	= user_describe,
	.read		= user_read,
	.request_key	= nfs_idmap_legacy_upcall,
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
	struct net *net = clp->cl_net;
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
	struct net *net = clp->cl_net;
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
	mutex_init(&idmap->idmap_mutex);

	clp->cl_idmap = idmap;
	return 0;
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

static struct nfs_client *nfs_get_client_for_event(struct net *net, int event)
{
	struct nfs_net *nn = net_generic(net, nfs_net_id);
	struct dentry *cl_dentry;
	struct nfs_client *clp;
	int err;

restart:
	spin_lock(&nn->nfs_client_lock);
	list_for_each_entry(clp, &nn->nfs_client_list, cl_share_link) {
		/* Wait for initialisation to finish */
		if (clp->cl_cons_state == NFS_CS_INITING) {
			atomic_inc(&clp->cl_count);
			spin_unlock(&nn->nfs_client_lock);
			err = nfs_wait_client_init_complete(clp);
			nfs_put_client(clp);
			if (err)
				return NULL;
			goto restart;
		}
		/* Skip nfs_clients that failed to initialise */
		if (clp->cl_cons_state < 0)
			continue;
		smp_rmb();
		if (clp->rpc_ops != &nfs_v4_clientops)
			continue;
		cl_dentry = clp->cl_idmap->idmap_pipe->dentry;
		if (((event == RPC_PIPEFS_MOUNT) && cl_dentry) ||
		    ((event == RPC_PIPEFS_UMOUNT) && !cl_dentry))
			continue;
		atomic_inc(&clp->cl_count);
		spin_unlock(&nn->nfs_client_lock);
		return clp;
	}
	spin_unlock(&nn->nfs_client_lock);
	return NULL;
}

static int rpc_pipefs_event(struct notifier_block *nb, unsigned long event,
			    void *ptr)
{
	struct super_block *sb = ptr;
	struct nfs_client *clp;
	int error = 0;

	if (!try_module_get(THIS_MODULE))
		return 0;

	while ((clp = nfs_get_client_for_event(sb->s_fs_info, event))) {
		error = __rpc_pipefs_event(clp, event, sb);
		nfs_put_client(clp);
		if (error)
			break;
	}
	module_put(THIS_MODULE);
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

static int nfs_idmap_prepare_message(char *desc, struct idmap_msg *im,
				     struct rpc_pipe_msg *msg)
{
	substring_t substr;
	int token, ret;

	memset(im,  0, sizeof(*im));
	memset(msg, 0, sizeof(*msg));

	im->im_type = IDMAP_TYPE_GROUP;
	token = match_token(desc, nfs_idmap_tokens, &substr);

	switch (token) {
	case Opt_find_uid:
		im->im_type = IDMAP_TYPE_USER;
	case Opt_find_gid:
		im->im_conv = IDMAP_CONV_NAMETOID;
		ret = match_strlcpy(im->im_name, &substr, IDMAP_NAMESZ);
		break;

	case Opt_find_user:
		im->im_type = IDMAP_TYPE_USER;
	case Opt_find_group:
		im->im_conv = IDMAP_CONV_IDTONAME;
		ret = match_int(&substr, &im->im_id);
		break;

	default:
		ret = -EINVAL;
		goto out;
	}

	msg->data = im;
	msg->len  = sizeof(struct idmap_msg);

out:
	return ret;
}

static int nfs_idmap_legacy_upcall(struct key_construction *cons,
				   const char *op,
				   void *aux)
{
	struct rpc_pipe_msg *msg;
	struct idmap_msg *im;
	struct idmap *idmap = (struct idmap *)aux;
	struct key *key = cons->key;
	int ret = -ENOMEM;

	/* msg and im are freed in idmap_pipe_destroy_msg */
	msg = kmalloc(sizeof(*msg), GFP_KERNEL);
	if (!msg)
		goto out0;

	im = kmalloc(sizeof(*im), GFP_KERNEL);
	if (!im)
		goto out1;

	ret = nfs_idmap_prepare_message(key->description, im, msg);
	if (ret < 0)
		goto out2;

	idmap->idmap_key_cons = cons;

	ret = rpc_queue_upcall(idmap->idmap_pipe, msg);
	if (ret < 0)
		goto out2;

	return ret;

out2:
	kfree(im);
out1:
	kfree(msg);
out0:
	key_revoke(cons->key);
	key_revoke(cons->authkey);
	return ret;
}

static int nfs_idmap_instantiate(struct key *key, struct key *authkey, char *data)
{
	return key_instantiate_and_link(key, data, strlen(data) + 1,
					id_resolver_cache->thread_keyring,
					authkey);
}

static int nfs_idmap_read_message(struct idmap_msg *im, struct key *key, struct key *authkey)
{
	char id_str[NFS_UINT_MAXLEN];
	int ret = -EINVAL;

	switch (im->im_conv) {
	case IDMAP_CONV_NAMETOID:
		sprintf(id_str, "%d", im->im_id);
		ret = nfs_idmap_instantiate(key, authkey, id_str);
		break;
	case IDMAP_CONV_IDTONAME:
		ret = nfs_idmap_instantiate(key, authkey, im->im_name);
		break;
	}

	return ret;
}

static ssize_t
idmap_pipe_downcall(struct file *filp, const char __user *src, size_t mlen)
{
	struct rpc_inode *rpci = RPC_I(filp->f_path.dentry->d_inode);
	struct idmap *idmap = (struct idmap *)rpci->private;
	struct key_construction *cons = idmap->idmap_key_cons;
	struct idmap_msg im;
	size_t namelen_in;
	int ret;

	if (mlen != sizeof(im)) {
		ret = -ENOSPC;
		goto out;
	}

	if (copy_from_user(&im, src, mlen) != 0) {
		ret = -EFAULT;
		goto out;
	}

	if (!(im.im_status & IDMAP_STATUS_SUCCESS)) {
		ret = mlen;
		complete_request_key(idmap->idmap_key_cons, -ENOKEY);
		goto out_incomplete;
	}

	namelen_in = strnlen(im.im_name, IDMAP_NAMESZ);
	if (namelen_in == 0 || namelen_in == IDMAP_NAMESZ) {
		ret = -EINVAL;
		goto out;
	}

	ret = nfs_idmap_read_message(&im, cons->key, cons->authkey);
	if (ret >= 0) {
		key_set_timeout(cons->key, nfs_idmap_cache_timeout);
		ret = mlen;
	}

out:
	complete_request_key(idmap->idmap_key_cons, ret);
out_incomplete:
	return ret;
}

static void
idmap_pipe_destroy_msg(struct rpc_pipe_msg *msg)
{
	/* Free memory allocated in nfs_idmap_legacy_upcall() */
	kfree(msg->data);
	kfree(msg);
}

int nfs_map_name_to_uid(const struct nfs_server *server, const char *name, size_t namelen, __u32 *uid)
{
	struct idmap *idmap = server->nfs_client->cl_idmap;

	if (nfs_map_string_to_numeric(name, namelen, uid))
		return 0;
	return nfs_idmap_lookup_id(name, namelen, "uid", uid, idmap);
}

int nfs_map_group_to_gid(const struct nfs_server *server, const char *name, size_t namelen, __u32 *gid)
{
	struct idmap *idmap = server->nfs_client->cl_idmap;

	if (nfs_map_string_to_numeric(name, namelen, gid))
		return 0;
	return nfs_idmap_lookup_id(name, namelen, "gid", gid, idmap);
}

int nfs_map_uid_to_name(const struct nfs_server *server, __u32 uid, char *buf, size_t buflen)
{
	struct idmap *idmap = server->nfs_client->cl_idmap;
	int ret = -EINVAL;

	if (!(server->caps & NFS_CAP_UIDGID_NOMAP))
		ret = nfs_idmap_lookup_name(uid, "user", buf, buflen, idmap);
	if (ret < 0)
		ret = nfs_map_numeric_to_string(uid, buf, buflen);
	return ret;
}
int nfs_map_gid_to_group(const struct nfs_server *server, __u32 gid, char *buf, size_t buflen)
{
	struct idmap *idmap = server->nfs_client->cl_idmap;
	int ret = -EINVAL;

	if (!(server->caps & NFS_CAP_UIDGID_NOMAP))
		ret = nfs_idmap_lookup_name(gid, "group", buf, buflen, idmap);
	if (ret < 0)
		ret = nfs_map_numeric_to_string(gid, buf, buflen);
	return ret;
}
