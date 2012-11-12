/*
*  Copyright (c) 2004 The Regents of the University of Michigan.
*  Copyright (c) 2012 Jeff Layton <jlayton@redhat.com>
*  All rights reserved.
*
*  Andy Adamson <andros@citi.umich.edu>
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
*/

#include <linux/file.h>
#include <linux/slab.h>
#include <linux/namei.h>
#include <linux/crypto.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <net/net_namespace.h>
#include <linux/sunrpc/rpc_pipe_fs.h>
#include <linux/sunrpc/clnt.h>
#include <linux/nfsd/cld.h>

#include "nfsd.h"
#include "state.h"
#include "vfs.h"
#include "netns.h"

#define NFSDDBG_FACILITY                NFSDDBG_PROC

/* Declarations */
struct nfsd4_client_tracking_ops {
	int (*init)(struct net *);
	void (*exit)(struct net *);
	void (*create)(struct nfs4_client *);
	void (*remove)(struct nfs4_client *);
	int (*check)(struct nfs4_client *);
	void (*grace_done)(struct net *, time_t);
};

/* Globals */
static struct file *rec_file;
static char user_recovery_dirname[PATH_MAX] = "/var/lib/nfs/v4recovery";
static struct nfsd4_client_tracking_ops *client_tracking_ops;

static int
nfs4_save_creds(const struct cred **original_creds)
{
	struct cred *new;

	new = prepare_creds();
	if (!new)
		return -ENOMEM;

	new->fsuid = 0;
	new->fsgid = 0;
	*original_creds = override_creds(new);
	put_cred(new);
	return 0;
}

static void
nfs4_reset_creds(const struct cred *original)
{
	revert_creds(original);
}

static void
md5_to_hex(char *out, char *md5)
{
	int i;

	for (i=0; i<16; i++) {
		unsigned char c = md5[i];

		*out++ = '0' + ((c&0xf0)>>4) + (c>=0xa0)*('a'-'9'-1);
		*out++ = '0' + (c&0x0f) + ((c&0x0f)>=0x0a)*('a'-'9'-1);
	}
	*out = '\0';
}

__be32
nfs4_make_rec_clidname(char *dname, struct xdr_netobj *clname)
{
	struct xdr_netobj cksum;
	struct hash_desc desc;
	struct scatterlist sg;
	__be32 status = nfserr_jukebox;

	dprintk("NFSD: nfs4_make_rec_clidname for %.*s\n",
			clname->len, clname->data);
	desc.flags = CRYPTO_TFM_REQ_MAY_SLEEP;
	desc.tfm = crypto_alloc_hash("md5", 0, CRYPTO_ALG_ASYNC);
	if (IS_ERR(desc.tfm))
		goto out_no_tfm;
	cksum.len = crypto_hash_digestsize(desc.tfm);
	cksum.data = kmalloc(cksum.len, GFP_KERNEL);
	if (cksum.data == NULL)
 		goto out;

	sg_init_one(&sg, clname->data, clname->len);

	if (crypto_hash_digest(&desc, &sg, sg.length, cksum.data))
		goto out;

	md5_to_hex(dname, cksum.data);

	status = nfs_ok;
out:
	kfree(cksum.data);
	crypto_free_hash(desc.tfm);
out_no_tfm:
	return status;
}

static void
nfsd4_create_clid_dir(struct nfs4_client *clp)
{
	const struct cred *original_cred;
	char *dname = clp->cl_recdir;
	struct dentry *dir, *dentry;
	int status;

	dprintk("NFSD: nfsd4_create_clid_dir for \"%s\"\n", dname);

	if (test_and_set_bit(NFSD4_CLIENT_STABLE, &clp->cl_flags))
		return;
	if (!rec_file)
		return;
	status = nfs4_save_creds(&original_cred);
	if (status < 0)
		return;

	status = mnt_want_write_file(rec_file);
	if (status)
		return;

	dir = rec_file->f_path.dentry;
	/* lock the parent */
	mutex_lock(&dir->d_inode->i_mutex);

	dentry = lookup_one_len(dname, dir, HEXDIR_LEN-1);
	if (IS_ERR(dentry)) {
		status = PTR_ERR(dentry);
		goto out_unlock;
	}
	if (dentry->d_inode)
		/*
		 * In the 4.1 case, where we're called from
		 * reclaim_complete(), records from the previous reboot
		 * may still be left, so this is OK.
		 *
		 * In the 4.0 case, we should never get here; but we may
		 * as well be forgiving and just succeed silently.
		 */
		goto out_put;
	status = vfs_mkdir(dir->d_inode, dentry, S_IRWXU);
out_put:
	dput(dentry);
out_unlock:
	mutex_unlock(&dir->d_inode->i_mutex);
	if (status == 0)
		vfs_fsync(rec_file, 0);
	else
		printk(KERN_ERR "NFSD: failed to write recovery record"
				" (err %d); please check that %s exists"
				" and is writeable", status,
				user_recovery_dirname);
	mnt_drop_write_file(rec_file);
	nfs4_reset_creds(original_cred);
}

typedef int (recdir_func)(struct dentry *, struct dentry *);

struct name_list {
	char name[HEXDIR_LEN];
	struct list_head list;
};

static int
nfsd4_build_namelist(void *arg, const char *name, int namlen,
		loff_t offset, u64 ino, unsigned int d_type)
{
	struct list_head *names = arg;
	struct name_list *entry;

	if (namlen != HEXDIR_LEN - 1)
		return 0;
	entry = kmalloc(sizeof(struct name_list), GFP_KERNEL);
	if (entry == NULL)
		return -ENOMEM;
	memcpy(entry->name, name, HEXDIR_LEN - 1);
	entry->name[HEXDIR_LEN - 1] = '\0';
	list_add(&entry->list, names);
	return 0;
}

static int
nfsd4_list_rec_dir(recdir_func *f)
{
	const struct cred *original_cred;
	struct dentry *dir = rec_file->f_path.dentry;
	LIST_HEAD(names);
	int status;

	status = nfs4_save_creds(&original_cred);
	if (status < 0)
		return status;

	status = vfs_llseek(rec_file, 0, SEEK_SET);
	if (status < 0) {
		nfs4_reset_creds(original_cred);
		return status;
	}

	status = vfs_readdir(rec_file, nfsd4_build_namelist, &names);
	mutex_lock_nested(&dir->d_inode->i_mutex, I_MUTEX_PARENT);
	while (!list_empty(&names)) {
		struct name_list *entry;
		entry = list_entry(names.next, struct name_list, list);
		if (!status) {
			struct dentry *dentry;
			dentry = lookup_one_len(entry->name, dir, HEXDIR_LEN-1);
			if (IS_ERR(dentry)) {
				status = PTR_ERR(dentry);
				break;
			}
			status = f(dir, dentry);
			dput(dentry);
		}
		list_del(&entry->list);
		kfree(entry);
	}
	mutex_unlock(&dir->d_inode->i_mutex);
	nfs4_reset_creds(original_cred);
	return status;
}

static int
nfsd4_unlink_clid_dir(char *name, int namlen)
{
	struct dentry *dir, *dentry;
	int status;

	dprintk("NFSD: nfsd4_unlink_clid_dir. name %.*s\n", namlen, name);

	dir = rec_file->f_path.dentry;
	mutex_lock_nested(&dir->d_inode->i_mutex, I_MUTEX_PARENT);
	dentry = lookup_one_len(name, dir, namlen);
	if (IS_ERR(dentry)) {
		status = PTR_ERR(dentry);
		goto out_unlock;
	}
	status = -ENOENT;
	if (!dentry->d_inode)
		goto out;
	status = vfs_rmdir(dir->d_inode, dentry);
out:
	dput(dentry);
out_unlock:
	mutex_unlock(&dir->d_inode->i_mutex);
	return status;
}

static void
nfsd4_remove_clid_dir(struct nfs4_client *clp)
{
	const struct cred *original_cred;
	int status;

	if (!rec_file || !test_bit(NFSD4_CLIENT_STABLE, &clp->cl_flags))
		return;

	status = mnt_want_write_file(rec_file);
	if (status)
		goto out;
	clear_bit(NFSD4_CLIENT_STABLE, &clp->cl_flags);

	status = nfs4_save_creds(&original_cred);
	if (status < 0)
		goto out_drop_write;

	status = nfsd4_unlink_clid_dir(clp->cl_recdir, HEXDIR_LEN-1);
	nfs4_reset_creds(original_cred);
	if (status == 0)
		vfs_fsync(rec_file, 0);
out_drop_write:
	mnt_drop_write_file(rec_file);
out:
	if (status)
		printk("NFSD: Failed to remove expired client state directory"
				" %.*s\n", HEXDIR_LEN, clp->cl_recdir);
}

static int
purge_old(struct dentry *parent, struct dentry *child)
{
	int status;

	if (nfs4_has_reclaimed_state(child->d_name.name))
		return 0;

	status = vfs_rmdir(parent->d_inode, child);
	if (status)
		printk("failed to remove client recovery directory %s\n",
				child->d_name.name);
	/* Keep trying, success or failure: */
	return 0;
}

static void
nfsd4_recdir_purge_old(struct net *net, time_t boot_time)
{
	int status;

	if (!rec_file)
		return;
	status = mnt_want_write_file(rec_file);
	if (status)
		goto out;
	status = nfsd4_list_rec_dir(purge_old);
	if (status == 0)
		vfs_fsync(rec_file, 0);
	mnt_drop_write_file(rec_file);
out:
	if (status)
		printk("nfsd4: failed to purge old clients from recovery"
			" directory %s\n", rec_file->f_path.dentry->d_name.name);
}

static int
load_recdir(struct dentry *parent, struct dentry *child)
{
	if (child->d_name.len != HEXDIR_LEN - 1) {
		printk("nfsd4: illegal name %s in recovery directory\n",
				child->d_name.name);
		/* Keep trying; maybe the others are OK: */
		return 0;
	}
	nfs4_client_to_reclaim(child->d_name.name);
	return 0;
}

static int
nfsd4_recdir_load(void) {
	int status;

	if (!rec_file)
		return 0;

	status = nfsd4_list_rec_dir(load_recdir);
	if (status)
		printk("nfsd4: failed loading clients from recovery"
			" directory %s\n", rec_file->f_path.dentry->d_name.name);
	return status;
}

/*
 * Hold reference to the recovery directory.
 */

static int
nfsd4_init_recdir(void)
{
	const struct cred *original_cred;
	int status;

	printk("NFSD: Using %s as the NFSv4 state recovery directory\n",
			user_recovery_dirname);

	BUG_ON(rec_file);

	status = nfs4_save_creds(&original_cred);
	if (status < 0) {
		printk("NFSD: Unable to change credentials to find recovery"
		       " directory: error %d\n",
		       status);
		return status;
	}

	rec_file = filp_open(user_recovery_dirname, O_RDONLY | O_DIRECTORY, 0);
	if (IS_ERR(rec_file)) {
		printk("NFSD: unable to find recovery directory %s\n",
				user_recovery_dirname);
		status = PTR_ERR(rec_file);
		rec_file = NULL;
	}

	nfs4_reset_creds(original_cred);
	return status;
}

static int
nfsd4_load_reboot_recovery_data(struct net *net)
{
	int status;

	/* XXX: The legacy code won't work in a container */
	if (net != &init_net) {
		WARN(1, KERN_ERR "NFSD: attempt to initialize legacy client "
			"tracking in a container!\n");
		return -EINVAL;
	}

	nfs4_lock_state();
	status = nfsd4_init_recdir();
	if (!status)
		status = nfsd4_recdir_load();
	nfs4_unlock_state();
	if (status)
		printk(KERN_ERR "NFSD: Failure reading reboot recovery data\n");
	return status;
}

static void
nfsd4_shutdown_recdir(void)
{
	if (!rec_file)
		return;
	fput(rec_file);
	rec_file = NULL;
}

static void
nfsd4_legacy_tracking_exit(struct net *net)
{
	nfs4_release_reclaim();
	nfsd4_shutdown_recdir();
}

/*
 * Change the NFSv4 recovery directory to recdir.
 */
int
nfs4_reset_recoverydir(char *recdir)
{
	int status;
	struct path path;

	status = kern_path(recdir, LOOKUP_FOLLOW, &path);
	if (status)
		return status;
	status = -ENOTDIR;
	if (S_ISDIR(path.dentry->d_inode->i_mode)) {
		strcpy(user_recovery_dirname, recdir);
		status = 0;
	}
	path_put(&path);
	return status;
}

char *
nfs4_recoverydir(void)
{
	return user_recovery_dirname;
}

static int
nfsd4_check_legacy_client(struct nfs4_client *clp)
{
	/* did we already find that this client is stable? */
	if (test_bit(NFSD4_CLIENT_STABLE, &clp->cl_flags))
		return 0;

	/* look for it in the reclaim hashtable otherwise */
	if (nfsd4_find_reclaim_client(clp)) {
		set_bit(NFSD4_CLIENT_STABLE, &clp->cl_flags);
		return 0;
	}

	return -ENOENT;
}

static struct nfsd4_client_tracking_ops nfsd4_legacy_tracking_ops = {
	.init		= nfsd4_load_reboot_recovery_data,
	.exit		= nfsd4_legacy_tracking_exit,
	.create		= nfsd4_create_clid_dir,
	.remove		= nfsd4_remove_clid_dir,
	.check		= nfsd4_check_legacy_client,
	.grace_done	= nfsd4_recdir_purge_old,
};

/* Globals */
#define NFSD_PIPE_DIR		"nfsd"
#define NFSD_CLD_PIPE		"cld"

/* per-net-ns structure for holding cld upcall info */
struct cld_net {
	struct rpc_pipe		*cn_pipe;
	spinlock_t		 cn_lock;
	struct list_head	 cn_list;
	unsigned int		 cn_xid;
};

struct cld_upcall {
	struct list_head	 cu_list;
	struct cld_net		*cu_net;
	struct task_struct	*cu_task;
	struct cld_msg		 cu_msg;
};

static int
__cld_pipe_upcall(struct rpc_pipe *pipe, struct cld_msg *cmsg)
{
	int ret;
	struct rpc_pipe_msg msg;

	memset(&msg, 0, sizeof(msg));
	msg.data = cmsg;
	msg.len = sizeof(*cmsg);

	/*
	 * Set task state before we queue the upcall. That prevents
	 * wake_up_process in the downcall from racing with schedule.
	 */
	set_current_state(TASK_UNINTERRUPTIBLE);
	ret = rpc_queue_upcall(pipe, &msg);
	if (ret < 0) {
		set_current_state(TASK_RUNNING);
		goto out;
	}

	schedule();
	set_current_state(TASK_RUNNING);

	if (msg.errno < 0)
		ret = msg.errno;
out:
	return ret;
}

static int
cld_pipe_upcall(struct rpc_pipe *pipe, struct cld_msg *cmsg)
{
	int ret;

	/*
	 * -EAGAIN occurs when pipe is closed and reopened while there are
	 *  upcalls queued.
	 */
	do {
		ret = __cld_pipe_upcall(pipe, cmsg);
	} while (ret == -EAGAIN);

	return ret;
}

static ssize_t
cld_pipe_downcall(struct file *filp, const char __user *src, size_t mlen)
{
	struct cld_upcall *tmp, *cup;
	struct cld_msg __user *cmsg = (struct cld_msg __user *)src;
	uint32_t xid;
	struct nfsd_net *nn = net_generic(filp->f_dentry->d_sb->s_fs_info,
						nfsd_net_id);
	struct cld_net *cn = nn->cld_net;

	if (mlen != sizeof(*cmsg)) {
		dprintk("%s: got %zu bytes, expected %zu\n", __func__, mlen,
			sizeof(*cmsg));
		return -EINVAL;
	}

	/* copy just the xid so we can try to find that */
	if (copy_from_user(&xid, &cmsg->cm_xid, sizeof(xid)) != 0) {
		dprintk("%s: error when copying xid from userspace", __func__);
		return -EFAULT;
	}

	/* walk the list and find corresponding xid */
	cup = NULL;
	spin_lock(&cn->cn_lock);
	list_for_each_entry(tmp, &cn->cn_list, cu_list) {
		if (get_unaligned(&tmp->cu_msg.cm_xid) == xid) {
			cup = tmp;
			list_del_init(&cup->cu_list);
			break;
		}
	}
	spin_unlock(&cn->cn_lock);

	/* couldn't find upcall? */
	if (!cup) {
		dprintk("%s: couldn't find upcall -- xid=%u\n", __func__, xid);
		return -EINVAL;
	}

	if (copy_from_user(&cup->cu_msg, src, mlen) != 0)
		return -EFAULT;

	wake_up_process(cup->cu_task);
	return mlen;
}

static void
cld_pipe_destroy_msg(struct rpc_pipe_msg *msg)
{
	struct cld_msg *cmsg = msg->data;
	struct cld_upcall *cup = container_of(cmsg, struct cld_upcall,
						 cu_msg);

	/* errno >= 0 means we got a downcall */
	if (msg->errno >= 0)
		return;

	wake_up_process(cup->cu_task);
}

static const struct rpc_pipe_ops cld_upcall_ops = {
	.upcall		= rpc_pipe_generic_upcall,
	.downcall	= cld_pipe_downcall,
	.destroy_msg	= cld_pipe_destroy_msg,
};

static struct dentry *
nfsd4_cld_register_sb(struct super_block *sb, struct rpc_pipe *pipe)
{
	struct dentry *dir, *dentry;

	dir = rpc_d_lookup_sb(sb, NFSD_PIPE_DIR);
	if (dir == NULL)
		return ERR_PTR(-ENOENT);
	dentry = rpc_mkpipe_dentry(dir, NFSD_CLD_PIPE, NULL, pipe);
	dput(dir);
	return dentry;
}

static void
nfsd4_cld_unregister_sb(struct rpc_pipe *pipe)
{
	if (pipe->dentry)
		rpc_unlink(pipe->dentry);
}

static struct dentry *
nfsd4_cld_register_net(struct net *net, struct rpc_pipe *pipe)
{
	struct super_block *sb;
	struct dentry *dentry;

	sb = rpc_get_sb_net(net);
	if (!sb)
		return NULL;
	dentry = nfsd4_cld_register_sb(sb, pipe);
	rpc_put_sb_net(net);
	return dentry;
}

static void
nfsd4_cld_unregister_net(struct net *net, struct rpc_pipe *pipe)
{
	struct super_block *sb;

	sb = rpc_get_sb_net(net);
	if (sb) {
		nfsd4_cld_unregister_sb(pipe);
		rpc_put_sb_net(net);
	}
}

/* Initialize rpc_pipefs pipe for communication with client tracking daemon */
static int
nfsd4_init_cld_pipe(struct net *net)
{
	int ret;
	struct dentry *dentry;
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);
	struct cld_net *cn;

	if (nn->cld_net)
		return 0;

	cn = kzalloc(sizeof(*cn), GFP_KERNEL);
	if (!cn) {
		ret = -ENOMEM;
		goto err;
	}

	cn->cn_pipe = rpc_mkpipe_data(&cld_upcall_ops, RPC_PIPE_WAIT_FOR_OPEN);
	if (IS_ERR(cn->cn_pipe)) {
		ret = PTR_ERR(cn->cn_pipe);
		goto err;
	}
	spin_lock_init(&cn->cn_lock);
	INIT_LIST_HEAD(&cn->cn_list);

	dentry = nfsd4_cld_register_net(net, cn->cn_pipe);
	if (IS_ERR(dentry)) {
		ret = PTR_ERR(dentry);
		goto err_destroy_data;
	}

	cn->cn_pipe->dentry = dentry;
	nn->cld_net = cn;
	return 0;

err_destroy_data:
	rpc_destroy_pipe_data(cn->cn_pipe);
err:
	kfree(cn);
	printk(KERN_ERR "NFSD: unable to create nfsdcld upcall pipe (%d)\n",
			ret);
	return ret;
}

static void
nfsd4_remove_cld_pipe(struct net *net)
{
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);
	struct cld_net *cn = nn->cld_net;

	nfsd4_cld_unregister_net(net, cn->cn_pipe);
	rpc_destroy_pipe_data(cn->cn_pipe);
	kfree(nn->cld_net);
	nn->cld_net = NULL;
}

static struct cld_upcall *
alloc_cld_upcall(struct cld_net *cn)
{
	struct cld_upcall *new, *tmp;

	new = kzalloc(sizeof(*new), GFP_KERNEL);
	if (!new)
		return new;

	/* FIXME: hard cap on number in flight? */
restart_search:
	spin_lock(&cn->cn_lock);
	list_for_each_entry(tmp, &cn->cn_list, cu_list) {
		if (tmp->cu_msg.cm_xid == cn->cn_xid) {
			cn->cn_xid++;
			spin_unlock(&cn->cn_lock);
			goto restart_search;
		}
	}
	new->cu_task = current;
	new->cu_msg.cm_vers = CLD_UPCALL_VERSION;
	put_unaligned(cn->cn_xid++, &new->cu_msg.cm_xid);
	new->cu_net = cn;
	list_add(&new->cu_list, &cn->cn_list);
	spin_unlock(&cn->cn_lock);

	dprintk("%s: allocated xid %u\n", __func__, new->cu_msg.cm_xid);

	return new;
}

static void
free_cld_upcall(struct cld_upcall *victim)
{
	struct cld_net *cn = victim->cu_net;

	spin_lock(&cn->cn_lock);
	list_del(&victim->cu_list);
	spin_unlock(&cn->cn_lock);
	kfree(victim);
}

/* Ask daemon to create a new record */
static void
nfsd4_cld_create(struct nfs4_client *clp)
{
	int ret;
	struct cld_upcall *cup;
	/* FIXME: determine net from clp */
	struct nfsd_net *nn = net_generic(&init_net, nfsd_net_id);
	struct cld_net *cn = nn->cld_net;

	/* Don't upcall if it's already stored */
	if (test_bit(NFSD4_CLIENT_STABLE, &clp->cl_flags))
		return;

	cup = alloc_cld_upcall(cn);
	if (!cup) {
		ret = -ENOMEM;
		goto out_err;
	}

	cup->cu_msg.cm_cmd = Cld_Create;
	cup->cu_msg.cm_u.cm_name.cn_len = clp->cl_name.len;
	memcpy(cup->cu_msg.cm_u.cm_name.cn_id, clp->cl_name.data,
			clp->cl_name.len);

	ret = cld_pipe_upcall(cn->cn_pipe, &cup->cu_msg);
	if (!ret) {
		ret = cup->cu_msg.cm_status;
		set_bit(NFSD4_CLIENT_STABLE, &clp->cl_flags);
	}

	free_cld_upcall(cup);
out_err:
	if (ret)
		printk(KERN_ERR "NFSD: Unable to create client "
				"record on stable storage: %d\n", ret);
}

/* Ask daemon to create a new record */
static void
nfsd4_cld_remove(struct nfs4_client *clp)
{
	int ret;
	struct cld_upcall *cup;
	/* FIXME: determine net from clp */
	struct nfsd_net *nn = net_generic(&init_net, nfsd_net_id);
	struct cld_net *cn = nn->cld_net;

	/* Don't upcall if it's already removed */
	if (!test_bit(NFSD4_CLIENT_STABLE, &clp->cl_flags))
		return;

	cup = alloc_cld_upcall(cn);
	if (!cup) {
		ret = -ENOMEM;
		goto out_err;
	}

	cup->cu_msg.cm_cmd = Cld_Remove;
	cup->cu_msg.cm_u.cm_name.cn_len = clp->cl_name.len;
	memcpy(cup->cu_msg.cm_u.cm_name.cn_id, clp->cl_name.data,
			clp->cl_name.len);

	ret = cld_pipe_upcall(cn->cn_pipe, &cup->cu_msg);
	if (!ret) {
		ret = cup->cu_msg.cm_status;
		clear_bit(NFSD4_CLIENT_STABLE, &clp->cl_flags);
	}

	free_cld_upcall(cup);
out_err:
	if (ret)
		printk(KERN_ERR "NFSD: Unable to remove client "
				"record from stable storage: %d\n", ret);
}

/* Check for presence of a record, and update its timestamp */
static int
nfsd4_cld_check(struct nfs4_client *clp)
{
	int ret;
	struct cld_upcall *cup;
	/* FIXME: determine net from clp */
	struct nfsd_net *nn = net_generic(&init_net, nfsd_net_id);
	struct cld_net *cn = nn->cld_net;

	/* Don't upcall if one was already stored during this grace pd */
	if (test_bit(NFSD4_CLIENT_STABLE, &clp->cl_flags))
		return 0;

	cup = alloc_cld_upcall(cn);
	if (!cup) {
		printk(KERN_ERR "NFSD: Unable to check client record on "
				"stable storage: %d\n", -ENOMEM);
		return -ENOMEM;
	}

	cup->cu_msg.cm_cmd = Cld_Check;
	cup->cu_msg.cm_u.cm_name.cn_len = clp->cl_name.len;
	memcpy(cup->cu_msg.cm_u.cm_name.cn_id, clp->cl_name.data,
			clp->cl_name.len);

	ret = cld_pipe_upcall(cn->cn_pipe, &cup->cu_msg);
	if (!ret) {
		ret = cup->cu_msg.cm_status;
		set_bit(NFSD4_CLIENT_STABLE, &clp->cl_flags);
	}

	free_cld_upcall(cup);
	return ret;
}

static void
nfsd4_cld_grace_done(struct net *net, time_t boot_time)
{
	int ret;
	struct cld_upcall *cup;
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);
	struct cld_net *cn = nn->cld_net;

	cup = alloc_cld_upcall(cn);
	if (!cup) {
		ret = -ENOMEM;
		goto out_err;
	}

	cup->cu_msg.cm_cmd = Cld_GraceDone;
	cup->cu_msg.cm_u.cm_gracetime = (int64_t)boot_time;
	ret = cld_pipe_upcall(cn->cn_pipe, &cup->cu_msg);
	if (!ret)
		ret = cup->cu_msg.cm_status;

	free_cld_upcall(cup);
out_err:
	if (ret)
		printk(KERN_ERR "NFSD: Unable to end grace period: %d\n", ret);
}

static struct nfsd4_client_tracking_ops nfsd4_cld_tracking_ops = {
	.init		= nfsd4_init_cld_pipe,
	.exit		= nfsd4_remove_cld_pipe,
	.create		= nfsd4_cld_create,
	.remove		= nfsd4_cld_remove,
	.check		= nfsd4_cld_check,
	.grace_done	= nfsd4_cld_grace_done,
};

/* upcall via usermodehelper */
static char cltrack_prog[PATH_MAX] = "/sbin/nfsdcltrack";
module_param_string(cltrack_prog, cltrack_prog, sizeof(cltrack_prog),
			S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(cltrack_prog, "Path to the nfsdcltrack upcall program");

static int
nfsd4_umh_cltrack_upcall(char *cmd, char *arg)
{
	char *envp[] = { NULL };
	char *argv[4];
	int ret;

	if (unlikely(!cltrack_prog[0])) {
		dprintk("%s: cltrack_prog is disabled\n", __func__);
		return -EACCES;
	}

	dprintk("%s: cmd: %s\n", __func__, cmd);
	dprintk("%s: arg: %s\n", __func__, arg ? arg : "(null)");

	argv[0] = (char *)cltrack_prog;
	argv[1] = cmd;
	argv[2] = arg;
	argv[3] = NULL;

	ret = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
	/*
	 * Disable the upcall mechanism if we're getting an ENOENT or EACCES
	 * error. The admin can re-enable it on the fly by using sysfs
	 * once the problem has been fixed.
	 */
	if (ret == -ENOENT || ret == -EACCES) {
		dprintk("NFSD: %s was not found or isn't executable (%d). "
			"Setting cltrack_prog to blank string!",
			cltrack_prog, ret);
		cltrack_prog[0] = '\0';
	}
	dprintk("%s: %s return value: %d\n", __func__, cltrack_prog, ret);

	return ret;
}

static char *
bin_to_hex_dup(const unsigned char *src, int srclen)
{
	int i;
	char *buf, *hex;

	/* +1 for terminating NULL */
	buf = kmalloc((srclen * 2) + 1, GFP_KERNEL);
	if (!buf)
		return buf;

	hex = buf;
	for (i = 0; i < srclen; i++) {
		sprintf(hex, "%2.2x", *src++);
		hex += 2;
	}
	return buf;
}

static int
nfsd4_umh_cltrack_init(struct net __attribute__((unused)) *net)
{
	return nfsd4_umh_cltrack_upcall("init", NULL);
}

static void
nfsd4_umh_cltrack_create(struct nfs4_client *clp)
{
	char *hexid;

	hexid = bin_to_hex_dup(clp->cl_name.data, clp->cl_name.len);
	if (!hexid) {
		dprintk("%s: can't allocate memory for upcall!\n", __func__);
		return;
	}
	nfsd4_umh_cltrack_upcall("create", hexid);
	kfree(hexid);
}

static void
nfsd4_umh_cltrack_remove(struct nfs4_client *clp)
{
	char *hexid;

	hexid = bin_to_hex_dup(clp->cl_name.data, clp->cl_name.len);
	if (!hexid) {
		dprintk("%s: can't allocate memory for upcall!\n", __func__);
		return;
	}
	nfsd4_umh_cltrack_upcall("remove", hexid);
	kfree(hexid);
}

static int
nfsd4_umh_cltrack_check(struct nfs4_client *clp)
{
	int ret;
	char *hexid;

	hexid = bin_to_hex_dup(clp->cl_name.data, clp->cl_name.len);
	if (!hexid) {
		dprintk("%s: can't allocate memory for upcall!\n", __func__);
		return -ENOMEM;
	}
	ret = nfsd4_umh_cltrack_upcall("check", hexid);
	kfree(hexid);
	return ret;
}

static void
nfsd4_umh_cltrack_grace_done(struct net __attribute__((unused)) *net,
				time_t boot_time)
{
	char timestr[22]; /* FIXME: better way to determine max size? */

	sprintf(timestr, "%ld", boot_time);
	nfsd4_umh_cltrack_upcall("gracedone", timestr);
}

static struct nfsd4_client_tracking_ops nfsd4_umh_tracking_ops = {
	.init		= nfsd4_umh_cltrack_init,
	.exit		= NULL,
	.create		= nfsd4_umh_cltrack_create,
	.remove		= nfsd4_umh_cltrack_remove,
	.check		= nfsd4_umh_cltrack_check,
	.grace_done	= nfsd4_umh_cltrack_grace_done,
};

int
nfsd4_client_tracking_init(struct net *net)
{
	int status;
	struct path path;

	if (!client_tracking_ops) {
		client_tracking_ops = &nfsd4_cld_tracking_ops;
		status = kern_path(nfs4_recoverydir(), LOOKUP_FOLLOW, &path);
		if (!status) {
			if (S_ISDIR(path.dentry->d_inode->i_mode))
				client_tracking_ops =
						&nfsd4_legacy_tracking_ops;
			path_put(&path);
		}
	}

	status = client_tracking_ops->init(net);
	if (status) {
		printk(KERN_WARNING "NFSD: Unable to initialize client "
				    "recovery tracking! (%d)\n", status);
		client_tracking_ops = NULL;
	}
	return status;
}

void
nfsd4_client_tracking_exit(struct net *net)
{
	if (client_tracking_ops) {
		if (client_tracking_ops->exit)
			client_tracking_ops->exit(net);
		client_tracking_ops = NULL;
	}
}

void
nfsd4_client_record_create(struct nfs4_client *clp)
{
	if (client_tracking_ops)
		client_tracking_ops->create(clp);
}

void
nfsd4_client_record_remove(struct nfs4_client *clp)
{
	if (client_tracking_ops)
		client_tracking_ops->remove(clp);
}

int
nfsd4_client_record_check(struct nfs4_client *clp)
{
	if (client_tracking_ops)
		return client_tracking_ops->check(clp);

	return -EOPNOTSUPP;
}

void
nfsd4_record_grace_done(struct net *net, time_t boot_time)
{
	if (client_tracking_ops)
		client_tracking_ops->grace_done(net, boot_time);
}

static int
rpc_pipefs_event(struct notifier_block *nb, unsigned long event, void *ptr)
{
	struct super_block *sb = ptr;
	struct net *net = sb->s_fs_info;
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);
	struct cld_net *cn = nn->cld_net;
	struct dentry *dentry;
	int ret = 0;

	if (!try_module_get(THIS_MODULE))
		return 0;

	if (!cn) {
		module_put(THIS_MODULE);
		return 0;
	}

	switch (event) {
	case RPC_PIPEFS_MOUNT:
		dentry = nfsd4_cld_register_sb(sb, cn->cn_pipe);
		if (IS_ERR(dentry)) {
			ret = PTR_ERR(dentry);
			break;
		}
		cn->cn_pipe->dentry = dentry;
		break;
	case RPC_PIPEFS_UMOUNT:
		if (cn->cn_pipe->dentry)
			nfsd4_cld_unregister_sb(cn->cn_pipe);
		break;
	default:
		ret = -ENOTSUPP;
		break;
	}
	module_put(THIS_MODULE);
	return ret;
}

static struct notifier_block nfsd4_cld_block = {
	.notifier_call = rpc_pipefs_event,
};

int
register_cld_notifier(void)
{
	return rpc_pipefs_notifier_register(&nfsd4_cld_block);
}

void
unregister_cld_notifier(void)
{
	rpc_pipefs_notifier_unregister(&nfsd4_cld_block);
}
