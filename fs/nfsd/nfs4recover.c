/*
*  linux/fs/nfsd/nfs4recover.c
*
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


#include <linux/sunrpc/svc.h>
#include <linux/nfsd/nfsd.h>
#include <linux/nfs4.h>
#include <linux/nfsd/state.h>
#include <linux/nfsd/xdr4.h>
#include <linux/param.h>
#include <linux/file.h>
#include <linux/namei.h>
#include <asm/uaccess.h>
#include <asm/scatterlist.h>
#include <linux/crypto.h>


#define NFSDDBG_FACILITY                NFSDDBG_PROC

/* Globals */
char recovery_dirname[PATH_MAX] = "/var/lib/nfs/v4recovery";
static struct nameidata rec_dir;
static int rec_dir_init = 0;

static void
nfs4_save_user(uid_t *saveuid, gid_t *savegid)
{
	*saveuid = current->fsuid;
	*savegid = current->fsgid;
	current->fsuid = 0;
	current->fsgid = 0;
}

static void
nfs4_reset_user(uid_t saveuid, gid_t savegid)
{
	current->fsuid = saveuid;
	current->fsgid = savegid;
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

int
nfs4_make_rec_clidname(char *dname, struct xdr_netobj *clname)
{
	struct xdr_netobj cksum;
	struct crypto_tfm *tfm;
	struct scatterlist sg[1];
	int status = nfserr_resource;

	dprintk("NFSD: nfs4_make_rec_clidname for %.*s\n",
			clname->len, clname->data);
	tfm = crypto_alloc_tfm("md5", 0);
	if (tfm == NULL)
		goto out;
	cksum.len = crypto_tfm_alg_digestsize(tfm);
	cksum.data = kmalloc(cksum.len, GFP_KERNEL);
	if (cksum.data == NULL)
 		goto out;
	crypto_digest_init(tfm);

	sg[0].page = virt_to_page(clname->data);
	sg[0].offset = offset_in_page(clname->data);
	sg[0].length = clname->len;

	crypto_digest_update(tfm, sg, 1);
	crypto_digest_final(tfm, cksum.data);

	md5_to_hex(dname, cksum.data);

	kfree(cksum.data);
	status = nfs_ok;
out:
	if (tfm)
		crypto_free_tfm(tfm);
	return status;
}

typedef int (recdir_func)(struct dentry *, struct dentry *);

struct dentry_list {
	struct dentry *dentry;
	struct list_head list;
};

struct dentry_list_arg {
	struct list_head dentries;
	struct dentry *parent;
};

static int
nfsd4_build_dentrylist(void *arg, const char *name, int namlen,
		loff_t offset, ino_t ino, unsigned int d_type)
{
	struct dentry_list_arg *dla = arg;
	struct list_head *dentries = &dla->dentries;
	struct dentry *parent = dla->parent;
	struct dentry *dentry;
	struct dentry_list *child;

	if (name && isdotent(name, namlen))
		return nfs_ok;
	dentry = lookup_one_len(name, parent, namlen);
	if (IS_ERR(dentry))
		return PTR_ERR(dentry);
	child = kmalloc(sizeof(*child), GFP_KERNEL);
	if (child == NULL)
		return -ENOMEM;
	child->dentry = dentry;
	list_add(&child->list, dentries);
	return 0;
}

static int
nfsd4_list_rec_dir(struct dentry *dir, recdir_func *f)
{
	struct file *filp;
	struct dentry_list_arg dla = {
		.parent = dir,
	};
	struct list_head *dentries = &dla.dentries;
	struct dentry_list *child;
	uid_t uid;
	gid_t gid;
	int status;

	if (!rec_dir_init)
		return 0;

	nfs4_save_user(&uid, &gid);

	filp = dentry_open(dget(dir), mntget(rec_dir.mnt),
			O_RDWR);
	status = PTR_ERR(filp);
	if (IS_ERR(filp))
		goto out;
	INIT_LIST_HEAD(dentries);
	status = vfs_readdir(filp, nfsd4_build_dentrylist, &dla);
	fput(filp);
	while (!list_empty(dentries)) {
		child = list_entry(dentries->next, struct dentry_list, list);
		status = f(dir, child->dentry);
		if (status)
			goto out;
		list_del(&child->list);
		dput(child->dentry);
		kfree(child);
	}
out:
	while (!list_empty(dentries)) {
		child = list_entry(dentries->next, struct dentry_list, list);
		list_del(&child->list);
		dput(child->dentry);
		kfree(child);
	}
	nfs4_reset_user(uid, gid);
	return status;
}

static int
load_recdir(struct dentry *parent, struct dentry *child)
{
	if (child->d_name.len != HEXDIR_LEN - 1) {
		printk("nfsd4: illegal name %s in recovery directory\n",
				child->d_name.name);
		/* Keep trying; maybe the others are OK: */
		return nfs_ok;
	}
	nfs4_client_to_reclaim(child->d_name.name);
	return nfs_ok;
}

int
nfsd4_recdir_load(void) {
	int status;

	status = nfsd4_list_rec_dir(rec_dir.dentry, load_recdir);
	if (status)
		printk("nfsd4: failed loading clients from recovery"
			" directory %s\n", rec_dir.dentry->d_name.name);
	return status;
}

/*
 * Hold reference to the recovery directory.
 */

void
nfsd4_init_recdir(char *rec_dirname)
{
	uid_t			uid = 0;
	gid_t			gid = 0;
	int 			status;

	printk("NFSD: Using %s as the NFSv4 state recovery directory\n",
			rec_dirname);

	BUG_ON(rec_dir_init);

	nfs4_save_user(&uid, &gid);

	status = path_lookup(rec_dirname, LOOKUP_FOLLOW, &rec_dir);
	if (status == -ENOENT)
		printk("NFSD: recovery directory %s doesn't exist\n",
				rec_dirname);

	if (!status)
		rec_dir_init = 1;
	nfs4_reset_user(uid, gid);
}

void
nfsd4_shutdown_recdir(void)
{
	if (!rec_dir_init)
		return;
	rec_dir_init = 0;
	path_release(&rec_dir);
}
