/*
*  Copyright (c) 2004 The Regents of the University of Michigan.
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

#include "nfsd.h"
#include "state.h"
#include "vfs.h"

#define NFSDDBG_FACILITY                NFSDDBG_PROC

/* Globals */
static struct file *rec_file;

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
	__be32 status = nfserr_resource;

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

int
nfsd4_create_clid_dir(struct nfs4_client *clp)
{
	const struct cred *original_cred;
	char *dname = clp->cl_recdir;
	struct dentry *dir, *dentry;
	int status;

	dprintk("NFSD: nfsd4_create_clid_dir for \"%s\"\n", dname);

	if (!rec_file || clp->cl_firststate)
		return 0;

	status = nfs4_save_creds(&original_cred);
	if (status < 0)
		return status;

	dir = rec_file->f_path.dentry;
	/* lock the parent */
	mutex_lock(&dir->d_inode->i_mutex);

	dentry = lookup_one_len(dname, dir, HEXDIR_LEN-1);
	if (IS_ERR(dentry)) {
		status = PTR_ERR(dentry);
		goto out_unlock;
	}
	status = -EEXIST;
	if (dentry->d_inode) {
		dprintk("NFSD: nfsd4_create_clid_dir: DIRECTORY EXISTS\n");
		goto out_put;
	}
	status = mnt_want_write(rec_file->f_path.mnt);
	if (status)
		goto out_put;
	status = vfs_mkdir(dir->d_inode, dentry, S_IRWXU);
	mnt_drop_write(rec_file->f_path.mnt);
out_put:
	dput(dentry);
out_unlock:
	mutex_unlock(&dir->d_inode->i_mutex);
	if (status == 0) {
		clp->cl_firststate = 1;
		vfs_fsync(rec_file, 0);
	}
	nfs4_reset_creds(original_cred);
	dprintk("NFSD: nfsd4_create_clid_dir returns %d\n", status);
	return status;
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
nfsd4_list_rec_dir(struct dentry *dir, recdir_func *f)
{
	const struct cred *original_cred;
	struct file *filp;
	LIST_HEAD(names);
	struct name_list *entry;
	struct dentry *dentry;
	int status;

	if (!rec_file)
		return 0;

	status = nfs4_save_creds(&original_cred);
	if (status < 0)
		return status;

	filp = dentry_open(dget(dir), mntget(rec_file->f_path.mnt), O_RDONLY,
			   current_cred());
	status = PTR_ERR(filp);
	if (IS_ERR(filp))
		goto out;
	status = vfs_readdir(filp, nfsd4_build_namelist, &names);
	fput(filp);
	mutex_lock_nested(&dir->d_inode->i_mutex, I_MUTEX_PARENT);
	while (!list_empty(&names)) {
		entry = list_entry(names.next, struct name_list, list);

		dentry = lookup_one_len(entry->name, dir, HEXDIR_LEN-1);
		if (IS_ERR(dentry)) {
			status = PTR_ERR(dentry);
			break;
		}
		status = f(dir, dentry);
		dput(dentry);
		if (status)
			break;
		list_del(&entry->list);
		kfree(entry);
	}
	mutex_unlock(&dir->d_inode->i_mutex);
out:
	while (!list_empty(&names)) {
		entry = list_entry(names.next, struct name_list, list);
		list_del(&entry->list);
		kfree(entry);
	}
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

void
nfsd4_remove_clid_dir(struct nfs4_client *clp)
{
	const struct cred *original_cred;
	int status;

	if (!rec_file || !clp->cl_firststate)
		return;

	status = mnt_want_write(rec_file->f_path.mnt);
	if (status)
		goto out;
	clp->cl_firststate = 0;

	status = nfs4_save_creds(&original_cred);
	if (status < 0)
		goto out;

	status = nfsd4_unlink_clid_dir(clp->cl_recdir, HEXDIR_LEN-1);
	nfs4_reset_creds(original_cred);
	if (status == 0)
		vfs_fsync(rec_file, 0);
	mnt_drop_write(rec_file->f_path.mnt);
out:
	if (status)
		printk("NFSD: Failed to remove expired client state directory"
				" %.*s\n", HEXDIR_LEN, clp->cl_recdir);
	return;
}

static int
purge_old(struct dentry *parent, struct dentry *child)
{
	int status;

	/* note: we currently use this path only for minorversion 0 */
	if (nfs4_has_reclaimed_state(child->d_name.name, false))
		return 0;

	status = vfs_rmdir(parent->d_inode, child);
	if (status)
		printk("failed to remove client recovery directory %s\n",
				child->d_name.name);
	/* Keep trying, success or failure: */
	return 0;
}

void
nfsd4_recdir_purge_old(void) {
	int status;

	if (!rec_file)
		return;
	status = mnt_want_write(rec_file->f_path.mnt);
	if (status)
		goto out;
	status = nfsd4_list_rec_dir(rec_file->f_path.dentry, purge_old);
	if (status == 0)
		vfs_fsync(rec_file, 0);
	mnt_drop_write(rec_file->f_path.mnt);
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

int
nfsd4_recdir_load(void) {
	int status;

	if (!rec_file)
		return 0;

	status = nfsd4_list_rec_dir(rec_file->f_path.dentry, load_recdir);
	if (status)
		printk("nfsd4: failed loading clients from recovery"
			" directory %s\n", rec_file->f_path.dentry->d_name.name);
	return status;
}

/*
 * Hold reference to the recovery directory.
 */

void
nfsd4_init_recdir(char *rec_dirname)
{
	const struct cred *original_cred;
	int status;

	printk("NFSD: Using %s as the NFSv4 state recovery directory\n",
			rec_dirname);

	BUG_ON(rec_file);

	status = nfs4_save_creds(&original_cred);
	if (status < 0) {
		printk("NFSD: Unable to change credentials to find recovery"
		       " directory: error %d\n",
		       status);
		return;
	}

	rec_file = filp_open(rec_dirname, O_RDONLY | O_DIRECTORY, 0);
	if (IS_ERR(rec_file)) {
		printk("NFSD: unable to find recovery directory %s\n",
				rec_dirname);
		rec_file = NULL;
	}

	nfs4_reset_creds(original_cred);
}

void
nfsd4_shutdown_recdir(void)
{
	if (!rec_file)
		return;
	fput(rec_file);
	rec_file = NULL;
}
