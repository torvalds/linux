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
#include "nfs4idmap.h"
#include "nfs4trace.h"

#define NFS_UINT_MAXLEN 11

static const struct cred *id_resolver_cache;
static struct key_type key_type_id_resolver_legacy;

struct idmap_legacy_upcalldata {
	struct rpc_pipe_msg pipe_msg;
	struct idmap_msg idmap_msg;
	struct key_construction	*key_cons;
	struct idmap *idmap;
};

struct idmap {
	struct rpc_pipe_dir_object idmap_pdo;
	struct rpc_pipe		*idmap_pipe;
	struct idmap_legacy_upcalldata *idmap_upcall_data;
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
	kuid_t uid;

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
	kgid_t gid;

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

int nfs_map_string_to_numeric(const char *name, size_t namelen, __u32 *res)
{
	unsigned long val;
	char buf[16];

	if (memchr(name, '@', namelen) != NULL || namelen >= sizeof(buf))
		return 0;
	memcpy(buf, name, namelen);
	buf[namelen] = '\0';
	if (kstrtoul(buf, 0, &val) != 0)
		return 0;
	*res = val;
	return 1;
}
EXPORT_SYMBOL_GPL(nfs_map_string_to_numeric);

static int nfs_map_numeric_to_string(__u32 id, char *buf, size_t buflen)
{
	return snprintf(buf, buflen, "%u", id);
}

static struct key_type key_type_id_resolver = {
	.name		= "id_resolver",
	.preparse	= user_preparse,
	.free_preparse	= user_free_preparse,
	.instantiate	= generic_key_instantiate,
	.revoke		= user_revoke,
	.destroy	= user_destroy,
	.describe	= user_describe,
	.read		= user_read,
};

int nfs_idmap_init(void)
{
	struct cred *cred;
	struct key *keyring;
	int ret = 0;

	printk(KERN_NOTICE "NFS: Registering the %s key type\n",
		key_type_id_resolver.name);

	cred = prepare_kernel_cred(NULL);
	if (!cred)
		return -ENOMEM;

	keyring = keyring_alloc(".id_resolver",
				GLOBAL_ROOT_UID, GLOBAL_ROOT_GID, cred,
				(KEY_POS_ALL & ~KEY_POS_SETATTR) |
				KEY_USR_VIEW | KEY_USR_READ,
				KEY_ALLOC_NOT_IN_QUOTA, NULL);
	if (IS_ERR(keyring)) {
		ret = PTR_ERR(keyring);
		goto failed_put_cred;
	}

	ret = register_key_type(&key_type_id_resolver);
	if (ret < 0)
		goto failed_put_key;

	ret = register_key_type(&key_type_id_resolver_legacy);
	if (ret < 0)
		goto failed_reg_legacy;

	set_bit(KEY_FLAG_ROOT_CAN_CLEAR, &keyring->flags);
	cred->thread_keyring = keyring;
	cred->jit_keyring = KEY_REQKEY_DEFL_THREAD_KEYRING;
	id_resolver_cache = cred;
	return 0;

failed_reg_legacy:
	unregister_key_type(&key_type_id_resolver);
failed_put_key:
	key_put(keyring);
failed_put_cred:
	put_cred(cred);
	return ret;
}

void nfs_idmap_quit(void)
{
	key_revoke(id_resolver_cache->thread_keyring);
	unregister_key_type(&key_type_id_resolver);
	unregister_key_type(&key_type_id_resolver_legacy);
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

static struct key *nfs_idmap_request_key(const char *name, size_t namelen,
					 const char *type, struct idmap *idmap)
{
	char *desc;
	struct key *rkey;
	ssize_t ret;

	ret = nfs_idmap_get_desc(name, namelen, type, strlen(type), &desc);
	if (ret <= 0)
		return ERR_PTR(ret);

	rkey = request_key(&key_type_id_resolver, desc, "");
	if (IS_ERR(rkey)) {
		mutex_lock(&idmap->idmap_mutex);
		rkey = request_key_with_auxdata(&key_type_id_resolver_legacy,
						desc, "", 0, idmap);
		mutex_unlock(&idmap->idmap_mutex);
	}
	if (!IS_ERR(rkey))
		set_bit(KEY_FLAG_ROOT_CAN_INVAL, &rkey->flags);

	kfree(desc);
	return rkey;
}

static ssize_t nfs_idmap_get_key(const char *name, size_t namelen,
				 const char *type, void *data,
				 size_t data_size, struct idmap *idmap)
{
	const struct cred *saved_cred;
	struct key *rkey;
	const struct user_key_payload *payload;
	ssize_t ret;

	saved_cred = override_creds(id_resolver_cache);
	rkey = nfs_idmap_request_key(name, namelen, type, idmap);
	revert_creds(saved_cred);

	if (IS_ERR(rkey)) {
		ret = PTR_ERR(rkey);
		goto out;
	}

	rcu_read_lock();
	rkey->perm |= KEY_USR_VIEW;

	ret = key_validate(rkey);
	if (ret < 0)
		goto out_up;

	payload = user_key_payload(rkey);
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
static ssize_t nfs_idmap_lookup_name(__u32 id, const char *type, char *buf,
				     size_t buflen, struct idmap *idmap)
{
	char id_str[NFS_UINT_MAXLEN];
	int id_len;
	ssize_t ret;

	id_len = nfs_map_numeric_to_string(id, id_str, sizeof(id_str));
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
		ret = kstrtol(id_str, 10, &id_long);
		*id = (__u32)id_long;
	}
	return ret;
}

/* idmap classic begins here */

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
static void idmap_release_pipe(struct inode *);
static void idmap_pipe_destroy_msg(struct rpc_pipe_msg *);

static const struct rpc_pipe_ops idmap_upcall_ops = {
	.upcall		= rpc_pipe_generic_upcall,
	.downcall	= idmap_pipe_downcall,
	.release_pipe	= idmap_release_pipe,
	.destroy_msg	= idmap_pipe_destroy_msg,
};

static struct key_type key_type_id_resolver_legacy = {
	.name		= "id_legacy",
	.preparse	= user_preparse,
	.free_preparse	= user_free_preparse,
	.instantiate	= generic_key_instantiate,
	.revoke		= user_revoke,
	.destroy	= user_destroy,
	.describe	= user_describe,
	.read		= user_read,
	.request_key	= nfs_idmap_legacy_upcall,
};

static void nfs_idmap_pipe_destroy(struct dentry *dir,
		struct rpc_pipe_dir_object *pdo)
{
	struct idmap *idmap = pdo->pdo_data;
	struct rpc_pipe *pipe = idmap->idmap_pipe;

	if (pipe->dentry) {
		rpc_unlink(pipe->dentry);
		pipe->dentry = NULL;
	}
}

static int nfs_idmap_pipe_create(struct dentry *dir,
		struct rpc_pipe_dir_object *pdo)
{
	struct idmap *idmap = pdo->pdo_data;
	struct rpc_pipe *pipe = idmap->idmap_pipe;
	struct dentry *dentry;

	dentry = rpc_mkpipe_dentry(dir, "idmap", idmap, pipe);
	if (IS_ERR(dentry))
		return PTR_ERR(dentry);
	pipe->dentry = dentry;
	return 0;
}

static const struct rpc_pipe_dir_object_ops nfs_idmap_pipe_dir_object_ops = {
	.create = nfs_idmap_pipe_create,
	.destroy = nfs_idmap_pipe_destroy,
};

int
nfs_idmap_new(struct nfs_client *clp)
{
	struct idmap *idmap;
	struct rpc_pipe *pipe;
	int error;

	idmap = kzalloc(sizeof(*idmap), GFP_KERNEL);
	if (idmap == NULL)
		return -ENOMEM;

	rpc_init_pipe_dir_object(&idmap->idmap_pdo,
			&nfs_idmap_pipe_dir_object_ops,
			idmap);

	pipe = rpc_mkpipe_data(&idmap_upcall_ops, 0);
	if (IS_ERR(pipe)) {
		error = PTR_ERR(pipe);
		goto err;
	}
	idmap->idmap_pipe = pipe;
	mutex_init(&idmap->idmap_mutex);

	error = rpc_add_pipe_dir_object(clp->cl_net,
			&clp->cl_rpcclient->cl_pipedir_objects,
			&idmap->idmap_pdo);
	if (error)
		goto err_destroy_pipe;

	clp->cl_idmap = idmap;
	return 0;
err_destroy_pipe:
	rpc_destroy_pipe_data(idmap->idmap_pipe);
err:
	kfree(idmap);
	return error;
}

void
nfs_idmap_delete(struct nfs_client *clp)
{
	struct idmap *idmap = clp->cl_idmap;

	if (!idmap)
		return;
	clp->cl_idmap = NULL;
	rpc_remove_pipe_dir_object(clp->cl_net,
			&clp->cl_rpcclient->cl_pipedir_objects,
			&idmap->idmap_pdo);
	rpc_destroy_pipe_data(idmap->idmap_pipe);
	kfree(idmap);
}

static int nfs_idmap_prepare_message(char *desc, struct idmap *idmap,
				     struct idmap_msg *im,
				     struct rpc_pipe_msg *msg)
{
	substring_t substr;
	int token, ret;

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

static bool
nfs_idmap_prepare_pipe_upcall(struct idmap *idmap,
		struct idmap_legacy_upcalldata *data)
{
	if (idmap->idmap_upcall_data != NULL) {
		WARN_ON_ONCE(1);
		return false;
	}
	idmap->idmap_upcall_data = data;
	return true;
}

static void
nfs_idmap_complete_pipe_upcall_locked(struct idmap *idmap, int ret)
{
	struct key_construction *cons = idmap->idmap_upcall_data->key_cons;

	kfree(idmap->idmap_upcall_data);
	idmap->idmap_upcall_data = NULL;
	complete_request_key(cons, ret);
}

static void
nfs_idmap_abort_pipe_upcall(struct idmap *idmap, int ret)
{
	if (idmap->idmap_upcall_data != NULL)
		nfs_idmap_complete_pipe_upcall_locked(idmap, ret);
}

static int nfs_idmap_legacy_upcall(struct key_construction *cons,
				   const char *op,
				   void *aux)
{
	struct idmap_legacy_upcalldata *data;
	struct rpc_pipe_msg *msg;
	struct idmap_msg *im;
	struct idmap *idmap = (struct idmap *)aux;
	struct key *key = cons->key;
	int ret = -ENOKEY;

	if (!aux)
		goto out1;

	/* msg and im are freed in idmap_pipe_destroy_msg */
	ret = -ENOMEM;
	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		goto out1;

	msg = &data->pipe_msg;
	im = &data->idmap_msg;
	data->idmap = idmap;
	data->key_cons = cons;

	ret = nfs_idmap_prepare_message(key->description, idmap, im, msg);
	if (ret < 0)
		goto out2;

	ret = -EAGAIN;
	if (!nfs_idmap_prepare_pipe_upcall(idmap, data))
		goto out2;

	ret = rpc_queue_upcall(idmap->idmap_pipe, msg);
	if (ret < 0)
		nfs_idmap_abort_pipe_upcall(idmap, ret);

	return ret;
out2:
	kfree(data);
out1:
	complete_request_key(cons, ret);
	return ret;
}

static int nfs_idmap_instantiate(struct key *key, struct key *authkey, char *data, size_t datalen)
{
	return key_instantiate_and_link(key, data, datalen,
					id_resolver_cache->thread_keyring,
					authkey);
}

static int nfs_idmap_read_and_verify_message(struct idmap_msg *im,
		struct idmap_msg *upcall,
		struct key *key, struct key *authkey)
{
	char id_str[NFS_UINT_MAXLEN];
	size_t len;
	int ret = -ENOKEY;

	/* ret = -ENOKEY */
	if (upcall->im_type != im->im_type || upcall->im_conv != im->im_conv)
		goto out;
	switch (im->im_conv) {
	case IDMAP_CONV_NAMETOID:
		if (strcmp(upcall->im_name, im->im_name) != 0)
			break;
		/* Note: here we store the NUL terminator too */
		len = 1 + nfs_map_numeric_to_string(im->im_id, id_str,
						    sizeof(id_str));
		ret = nfs_idmap_instantiate(key, authkey, id_str, len);
		break;
	case IDMAP_CONV_IDTONAME:
		if (upcall->im_id != im->im_id)
			break;
		len = strlen(im->im_name);
		ret = nfs_idmap_instantiate(key, authkey, im->im_name, len);
		break;
	default:
		ret = -EINVAL;
	}
out:
	return ret;
}

static ssize_t
idmap_pipe_downcall(struct file *filp, const char __user *src, size_t mlen)
{
	struct rpc_inode *rpci = RPC_I(file_inode(filp));
	struct idmap *idmap = (struct idmap *)rpci->private;
	struct key_construction *cons;
	struct idmap_msg im;
	size_t namelen_in;
	int ret = -ENOKEY;

	/* If instantiation is successful, anyone waiting for key construction
	 * will have been woken up and someone else may now have used
	 * idmap_key_cons - so after this point we may no longer touch it.
	 */
	if (idmap->idmap_upcall_data == NULL)
		goto out_noupcall;

	cons = idmap->idmap_upcall_data->key_cons;

	if (mlen != sizeof(im)) {
		ret = -ENOSPC;
		goto out;
	}

	if (copy_from_user(&im, src, mlen) != 0) {
		ret = -EFAULT;
		goto out;
	}

	if (!(im.im_status & IDMAP_STATUS_SUCCESS)) {
		ret = -ENOKEY;
		goto out;
	}

	namelen_in = strnlen(im.im_name, IDMAP_NAMESZ);
	if (namelen_in == 0 || namelen_in == IDMAP_NAMESZ) {
		ret = -EINVAL;
		goto out;
}

	ret = nfs_idmap_read_and_verify_message(&im,
			&idmap->idmap_upcall_data->idmap_msg,
			cons->key, cons->authkey);
	if (ret >= 0) {
		key_set_timeout(cons->key, nfs_idmap_cache_timeout);
		ret = mlen;
	}

out:
	nfs_idmap_complete_pipe_upcall_locked(idmap, ret);
out_noupcall:
	return ret;
}

static void
idmap_pipe_destroy_msg(struct rpc_pipe_msg *msg)
{
	struct idmap_legacy_upcalldata *data = container_of(msg,
			struct idmap_legacy_upcalldata,
			pipe_msg);
	struct idmap *idmap = data->idmap;

	if (msg->errno)
		nfs_idmap_abort_pipe_upcall(idmap, msg->errno);
}

static void
idmap_release_pipe(struct inode *inode)
{
	struct rpc_inode *rpci = RPC_I(inode);
	struct idmap *idmap = (struct idmap *)rpci->private;

	nfs_idmap_abort_pipe_upcall(idmap, -EPIPE);
}

int nfs_map_name_to_uid(const struct nfs_server *server, const char *name, size_t namelen, kuid_t *uid)
{
	struct idmap *idmap = server->nfs_client->cl_idmap;
	__u32 id = -1;
	int ret = 0;

	if (!nfs_map_string_to_numeric(name, namelen, &id))
		ret = nfs_idmap_lookup_id(name, namelen, "uid", &id, idmap);
	if (ret == 0) {
		*uid = make_kuid(&init_user_ns, id);
		if (!uid_valid(*uid))
			ret = -ERANGE;
	}
	trace_nfs4_map_name_to_uid(name, namelen, id, ret);
	return ret;
}

int nfs_map_group_to_gid(const struct nfs_server *server, const char *name, size_t namelen, kgid_t *gid)
{
	struct idmap *idmap = server->nfs_client->cl_idmap;
	__u32 id = -1;
	int ret = 0;

	if (!nfs_map_string_to_numeric(name, namelen, &id))
		ret = nfs_idmap_lookup_id(name, namelen, "gid", &id, idmap);
	if (ret == 0) {
		*gid = make_kgid(&init_user_ns, id);
		if (!gid_valid(*gid))
			ret = -ERANGE;
	}
	trace_nfs4_map_group_to_gid(name, namelen, id, ret);
	return ret;
}

int nfs_map_uid_to_name(const struct nfs_server *server, kuid_t uid, char *buf, size_t buflen)
{
	struct idmap *idmap = server->nfs_client->cl_idmap;
	int ret = -EINVAL;
	__u32 id;

	id = from_kuid(&init_user_ns, uid);
	if (!(server->caps & NFS_CAP_UIDGID_NOMAP))
		ret = nfs_idmap_lookup_name(id, "user", buf, buflen, idmap);
	if (ret < 0)
		ret = nfs_map_numeric_to_string(id, buf, buflen);
	trace_nfs4_map_uid_to_name(buf, ret, id, ret);
	return ret;
}
int nfs_map_gid_to_group(const struct nfs_server *server, kgid_t gid, char *buf, size_t buflen)
{
	struct idmap *idmap = server->nfs_client->cl_idmap;
	int ret = -EINVAL;
	__u32 id;

	id = from_kgid(&init_user_ns, gid);
	if (!(server->caps & NFS_CAP_UIDGID_NOMAP))
		ret = nfs_idmap_lookup_name(id, "group", buf, buflen, idmap);
	if (ret < 0)
		ret = nfs_map_numeric_to_string(id, buf, buflen);
	trace_nfs4_map_gid_to_group(buf, ret, id, ret);
	return ret;
}
