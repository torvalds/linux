// SPDX-License-Identifier: GPL-2.0
/*
 *  dir.c
 *
 *  Copyright (C) 1995, 1996 by Volker Lendecke
 *  Modified for big endian by J.F. Chadima and David S. Miller
 *  Modified 1997 Peter Waltenberg, Bill Hawes, David Woodhouse for 2.1 dcache
 *  Modified 1998, 1999 Wolfram Pienkoss for NLS
 *  Modified 1999 Wolfram Pienkoss for directory caching
 *  Modified 2000 Ben Harris, University of Cambridge for NFS NS meta-info
 *
 */


#include <linux/time.h>
#include <linux/errno.h>
#include <linux/stat.h>
#include <linux/kernel.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/namei.h>
#include <linux/uaccess.h>
#include <asm/byteorder.h>

#include "ncp_fs.h"

static void ncp_read_volume_list(struct file *, struct dir_context *,
				struct ncp_cache_control *);
static void ncp_do_readdir(struct file *, struct dir_context *,
				struct ncp_cache_control *);

static int ncp_readdir(struct file *, struct dir_context *);

static int ncp_create(struct inode *, struct dentry *, umode_t, bool);
static struct dentry *ncp_lookup(struct inode *, struct dentry *, unsigned int);
static int ncp_unlink(struct inode *, struct dentry *);
static int ncp_mkdir(struct inode *, struct dentry *, umode_t);
static int ncp_rmdir(struct inode *, struct dentry *);
static int ncp_rename(struct inode *, struct dentry *,
		      struct inode *, struct dentry *, unsigned int);
static int ncp_mknod(struct inode * dir, struct dentry *dentry,
		     umode_t mode, dev_t rdev);
#if defined(CONFIG_NCPFS_EXTRAS) || defined(CONFIG_NCPFS_NFS_NS)
extern int ncp_symlink(struct inode *, struct dentry *, const char *);
#else
#define ncp_symlink NULL
#endif
		      
const struct file_operations ncp_dir_operations =
{
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
	.iterate	= ncp_readdir,
	.unlocked_ioctl	= ncp_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= ncp_compat_ioctl,
#endif
};

const struct inode_operations ncp_dir_inode_operations =
{
	.create		= ncp_create,
	.lookup		= ncp_lookup,
	.unlink		= ncp_unlink,
	.symlink	= ncp_symlink,
	.mkdir		= ncp_mkdir,
	.rmdir		= ncp_rmdir,
	.mknod		= ncp_mknod,
	.rename		= ncp_rename,
	.setattr	= ncp_notify_change,
};

/*
 * Dentry operations routines
 */
static int ncp_lookup_validate(struct dentry *, unsigned int);
static int ncp_hash_dentry(const struct dentry *, struct qstr *);
static int ncp_compare_dentry(const struct dentry *,
		unsigned int, const char *, const struct qstr *);
static int ncp_delete_dentry(const struct dentry *);
static void ncp_d_prune(struct dentry *dentry);

const struct dentry_operations ncp_dentry_operations =
{
	.d_revalidate	= ncp_lookup_validate,
	.d_hash		= ncp_hash_dentry,
	.d_compare	= ncp_compare_dentry,
	.d_delete	= ncp_delete_dentry,
	.d_prune	= ncp_d_prune,
};

#define ncp_namespace(i)	(NCP_SERVER(i)->name_space[NCP_FINFO(i)->volNumber])

static inline int ncp_preserve_entry_case(struct inode *i, __u32 nscreator)
{
#ifdef CONFIG_NCPFS_SMALLDOS
	int ns = ncp_namespace(i);

	if ((ns == NW_NS_DOS)
#ifdef CONFIG_NCPFS_OS2_NS
		|| ((ns == NW_NS_OS2) && (nscreator == NW_NS_DOS))
#endif /* CONFIG_NCPFS_OS2_NS */
	   )
		return 0;
#endif /* CONFIG_NCPFS_SMALLDOS */
	return 1;
}

#define ncp_preserve_case(i)	(ncp_namespace(i) != NW_NS_DOS)

static inline int ncp_case_sensitive(const struct inode *i)
{
#ifdef CONFIG_NCPFS_NFS_NS
	return ncp_namespace(i) == NW_NS_NFS;
#else
	return 0;
#endif /* CONFIG_NCPFS_NFS_NS */
}

/*
 * Note: leave the hash unchanged if the directory
 * is case-sensitive.
 *
 * Accessing the parent inode can be racy under RCU pathwalking.
 * Use ACCESS_ONCE() to make sure we use _one_ particular inode,
 * the callers will handle races.
 */
static int 
ncp_hash_dentry(const struct dentry *dentry, struct qstr *this)
{
	struct inode *inode = d_inode_rcu(dentry);

	if (!inode)
		return 0;

	if (!ncp_case_sensitive(inode)) {
		struct nls_table *t;
		unsigned long hash;
		int i;

		t = NCP_IO_TABLE(dentry->d_sb);
		hash = init_name_hash(dentry);
		for (i=0; i<this->len ; i++)
			hash = partial_name_hash(ncp_tolower(t, this->name[i]),
									hash);
		this->hash = end_name_hash(hash);
	}
	return 0;
}

/*
 * Accessing the parent inode can be racy under RCU pathwalking.
 * Use ACCESS_ONCE() to make sure we use _one_ particular inode,
 * the callers will handle races.
 */
static int
ncp_compare_dentry(const struct dentry *dentry,
		unsigned int len, const char *str, const struct qstr *name)
{
	struct inode *pinode;

	if (len != name->len)
		return 1;

	pinode = d_inode_rcu(dentry->d_parent);
	if (!pinode)
		return 1;

	if (ncp_case_sensitive(pinode))
		return strncmp(str, name->name, len);

	return ncp_strnicmp(NCP_IO_TABLE(pinode->i_sb), str, name->name, len);
}

/*
 * This is the callback from dput() when d_count is going to 0.
 * We use this to unhash dentries with bad inodes.
 * Closing files can be safely postponed until iput() - it's done there anyway.
 */
static int
ncp_delete_dentry(const struct dentry * dentry)
{
	struct inode *inode = d_inode(dentry);

	if (inode) {
		if (is_bad_inode(inode))
			return 1;
	} else
	{
	/* N.B. Unhash negative dentries? */
	}
	return 0;
}

static inline int
ncp_single_volume(struct ncp_server *server)
{
	return (server->m.mounted_vol[0] != '\0');
}

static inline int ncp_is_server_root(struct inode *inode)
{
	return !ncp_single_volume(NCP_SERVER(inode)) &&
		is_root_inode(inode);
}


/*
 * This is the callback when the dcache has a lookup hit.
 */


#ifdef CONFIG_NCPFS_STRONG
/* try to delete a readonly file (NW R bit set) */

static int
ncp_force_unlink(struct inode *dir, struct dentry* dentry)
{
        int res=0x9c,res2;
	struct nw_modify_dos_info info;
	__le32 old_nwattr;
	struct inode *inode;

	memset(&info, 0, sizeof(info));
	
        /* remove the Read-Only flag on the NW server */
	inode = d_inode(dentry);

	old_nwattr = NCP_FINFO(inode)->nwattr;
	info.attributes = old_nwattr & ~(aRONLY|aDELETEINHIBIT|aRENAMEINHIBIT);
	res2 = ncp_modify_file_or_subdir_dos_info_path(NCP_SERVER(inode), inode, NULL, DM_ATTRIBUTES, &info);
	if (res2)
		goto leave_me;

        /* now try again the delete operation */
        res = ncp_del_file_or_subdir2(NCP_SERVER(dir), dentry);

        if (res)  /* delete failed, set R bit again */
        {
		info.attributes = old_nwattr;
		res2 = ncp_modify_file_or_subdir_dos_info_path(NCP_SERVER(inode), inode, NULL, DM_ATTRIBUTES, &info);
		if (res2)
                        goto leave_me;
        }
leave_me:
        return(res);
}
#endif	/* CONFIG_NCPFS_STRONG */

#ifdef CONFIG_NCPFS_STRONG
static int
ncp_force_rename(struct inode *old_dir, struct dentry* old_dentry, char *_old_name,
                 struct inode *new_dir, struct dentry* new_dentry, char *_new_name)
{
	struct nw_modify_dos_info info;
        int res=0x90,res2;
	struct inode *old_inode = d_inode(old_dentry);
	__le32 old_nwattr = NCP_FINFO(old_inode)->nwattr;
	__le32 new_nwattr = 0; /* shut compiler warning */
	int old_nwattr_changed = 0;
	int new_nwattr_changed = 0;

	memset(&info, 0, sizeof(info));
	
        /* remove the Read-Only flag on the NW server */

	info.attributes = old_nwattr & ~(aRONLY|aRENAMEINHIBIT|aDELETEINHIBIT);
	res2 = ncp_modify_file_or_subdir_dos_info_path(NCP_SERVER(old_inode), old_inode, NULL, DM_ATTRIBUTES, &info);
	if (!res2)
		old_nwattr_changed = 1;
	if (new_dentry && d_really_is_positive(new_dentry)) {
		new_nwattr = NCP_FINFO(d_inode(new_dentry))->nwattr;
		info.attributes = new_nwattr & ~(aRONLY|aRENAMEINHIBIT|aDELETEINHIBIT);
		res2 = ncp_modify_file_or_subdir_dos_info_path(NCP_SERVER(new_dir), new_dir, _new_name, DM_ATTRIBUTES, &info);
		if (!res2)
			new_nwattr_changed = 1;
	}
        /* now try again the rename operation */
	/* but only if something really happened */
	if (new_nwattr_changed || old_nwattr_changed) {
	        res = ncp_ren_or_mov_file_or_subdir(NCP_SERVER(old_dir),
        	                                    old_dir, _old_name,
                	                            new_dir, _new_name);
	} 
	if (res)
		goto leave_me;
	/* file was successfully renamed, so:
	   do not set attributes on old file - it no longer exists
	   copy attributes from old file to new */
	new_nwattr_changed = old_nwattr_changed;
	new_nwattr = old_nwattr;
	old_nwattr_changed = 0;
	
leave_me:;
	if (old_nwattr_changed) {
		info.attributes = old_nwattr;
		res2 = ncp_modify_file_or_subdir_dos_info_path(NCP_SERVER(old_inode), old_inode, NULL, DM_ATTRIBUTES, &info);
		/* ignore errors */
	}
	if (new_nwattr_changed)	{
		info.attributes = new_nwattr;
		res2 = ncp_modify_file_or_subdir_dos_info_path(NCP_SERVER(new_dir), new_dir, _new_name, DM_ATTRIBUTES, &info);
		/* ignore errors */
	}
        return(res);
}
#endif	/* CONFIG_NCPFS_STRONG */


static int
ncp_lookup_validate(struct dentry *dentry, unsigned int flags)
{
	struct ncp_server *server;
	struct dentry *parent;
	struct inode *dir;
	struct ncp_entry_info finfo;
	int res, val = 0, len;
	__u8 __name[NCP_MAXPATHLEN + 1];

	if (dentry == dentry->d_sb->s_root)
		return 1;

	if (flags & LOOKUP_RCU)
		return -ECHILD;

	parent = dget_parent(dentry);
	dir = d_inode(parent);

	if (d_really_is_negative(dentry))
		goto finished;

	server = NCP_SERVER(dir);

	/*
	 * Inspired by smbfs:
	 * The default validation is based on dentry age:
	 * We set the max age at mount time.  (But each
	 * successful server lookup renews the timestamp.)
	 */
	val = NCP_TEST_AGE(server, dentry);
	if (val)
		goto finished;

	ncp_dbg(2, "%pd2 not valid, age=%ld, server lookup\n",
		dentry, NCP_GET_AGE(dentry));

	len = sizeof(__name);
	if (ncp_is_server_root(dir)) {
		res = ncp_io2vol(server, __name, &len, dentry->d_name.name,
				 dentry->d_name.len, 1);
		if (!res) {
			res = ncp_lookup_volume(server, __name, &(finfo.i));
			if (!res)
				ncp_update_known_namespace(server, finfo.i.volNumber, NULL);
		}
	} else {
		res = ncp_io2vol(server, __name, &len, dentry->d_name.name,
				 dentry->d_name.len, !ncp_preserve_case(dir));
		if (!res)
			res = ncp_obtain_info(server, dir, __name, &(finfo.i));
	}
	finfo.volume = finfo.i.volNumber;
	ncp_dbg(2, "looked for %pd/%s, res=%d\n",
		dentry->d_parent, __name, res);
	/*
	 * If we didn't find it, or if it has a different dirEntNum to
	 * what we remember, it's not valid any more.
	 */
	if (!res) {
		struct inode *inode = d_inode(dentry);

		inode_lock(inode);
		if (finfo.i.dirEntNum == NCP_FINFO(inode)->dirEntNum) {
			ncp_new_dentry(dentry);
			val=1;
		} else
			ncp_dbg(2, "found, but dirEntNum changed\n");

		ncp_update_inode2(inode, &finfo);
		inode_unlock(inode);
	}

finished:
	ncp_dbg(2, "result=%d\n", val);
	dput(parent);
	return val;
}

static time_t ncp_obtain_mtime(struct dentry *dentry)
{
	struct inode *inode = d_inode(dentry);
	struct ncp_server *server = NCP_SERVER(inode);
	struct nw_info_struct i;

	if (!ncp_conn_valid(server) || ncp_is_server_root(inode))
		return 0;

	if (ncp_obtain_info(server, inode, NULL, &i))
		return 0;

	return ncp_date_dos2unix(i.modifyTime, i.modifyDate);
}

static inline void
ncp_invalidate_dircache_entries(struct dentry *parent)
{
	struct ncp_server *server = NCP_SERVER(d_inode(parent));
	struct dentry *dentry;

	spin_lock(&parent->d_lock);
	list_for_each_entry(dentry, &parent->d_subdirs, d_child) {
		dentry->d_fsdata = NULL;
		ncp_age_dentry(server, dentry);
	}
	spin_unlock(&parent->d_lock);
}

static int ncp_readdir(struct file *file, struct dir_context *ctx)
{
	struct dentry *dentry = file->f_path.dentry;
	struct inode *inode = d_inode(dentry);
	struct page *page = NULL;
	struct ncp_server *server = NCP_SERVER(inode);
	union  ncp_dir_cache *cache = NULL;
	struct ncp_cache_control ctl;
	int result, mtime_valid = 0;
	time_t mtime = 0;

	ctl.page  = NULL;
	ctl.cache = NULL;

	ncp_dbg(2, "reading %pD2, pos=%d\n", file, (int)ctx->pos);

	result = -EIO;
	/* Do not generate '.' and '..' when server is dead. */
	if (!ncp_conn_valid(server))
		goto out;

	result = 0;
	if (!dir_emit_dots(file, ctx))
		goto out;

	page = grab_cache_page(&inode->i_data, 0);
	if (!page)
		goto read_really;

	ctl.cache = cache = kmap(page);
	ctl.head  = cache->head;

	if (!PageUptodate(page) || !ctl.head.eof)
		goto init_cache;

	if (ctx->pos == 2) {
		if (jiffies - ctl.head.time >= NCP_MAX_AGE(server))
			goto init_cache;

		mtime = ncp_obtain_mtime(dentry);
		mtime_valid = 1;
		if ((!mtime) || (mtime != ctl.head.mtime))
			goto init_cache;
	}

	if (ctx->pos > ctl.head.end)
		goto finished;

	ctl.fpos = ctx->pos + (NCP_DIRCACHE_START - 2);
	ctl.ofs  = ctl.fpos / NCP_DIRCACHE_SIZE;
	ctl.idx  = ctl.fpos % NCP_DIRCACHE_SIZE;

	for (;;) {
		if (ctl.ofs != 0) {
			ctl.page = find_lock_page(&inode->i_data, ctl.ofs);
			if (!ctl.page)
				goto invalid_cache;
			ctl.cache = kmap(ctl.page);
			if (!PageUptodate(ctl.page))
				goto invalid_cache;
		}
		while (ctl.idx < NCP_DIRCACHE_SIZE) {
			struct dentry *dent;
			bool over;

			spin_lock(&dentry->d_lock);
			if (!(NCP_FINFO(inode)->flags & NCPI_DIR_CACHE)) { 
				spin_unlock(&dentry->d_lock);
				goto invalid_cache;
			}
			dent = ctl.cache->dentry[ctl.idx];
			if (unlikely(!lockref_get_not_dead(&dent->d_lockref))) {
				spin_unlock(&dentry->d_lock);
				goto invalid_cache;
			}
			spin_unlock(&dentry->d_lock);
			if (d_really_is_negative(dent)) {
				dput(dent);
				goto invalid_cache;
			}
			over = !dir_emit(ctx, dent->d_name.name,
					dent->d_name.len,
					d_inode(dent)->i_ino, DT_UNKNOWN);
			dput(dent);
			if (over)
				goto finished;
			ctx->pos += 1;
			ctl.idx += 1;
			if (ctx->pos > ctl.head.end)
				goto finished;
		}
		if (ctl.page) {
			kunmap(ctl.page);
			SetPageUptodate(ctl.page);
			unlock_page(ctl.page);
			put_page(ctl.page);
			ctl.page = NULL;
		}
		ctl.idx  = 0;
		ctl.ofs += 1;
	}
invalid_cache:
	if (ctl.page) {
		kunmap(ctl.page);
		unlock_page(ctl.page);
		put_page(ctl.page);
		ctl.page = NULL;
	}
	ctl.cache = cache;
init_cache:
	ncp_invalidate_dircache_entries(dentry);
	if (!mtime_valid) {
		mtime = ncp_obtain_mtime(dentry);
		mtime_valid = 1;
	}
	ctl.head.mtime = mtime;
	ctl.head.time = jiffies;
	ctl.head.eof = 0;
	ctl.fpos = 2;
	ctl.ofs = 0;
	ctl.idx = NCP_DIRCACHE_START;
	ctl.filled = 0;
	ctl.valid  = 1;
read_really:
	spin_lock(&dentry->d_lock);
	NCP_FINFO(inode)->flags |= NCPI_DIR_CACHE;
	spin_unlock(&dentry->d_lock);
	if (ncp_is_server_root(inode)) {
		ncp_read_volume_list(file, ctx, &ctl);
	} else {
		ncp_do_readdir(file, ctx, &ctl);
	}
	ctl.head.end = ctl.fpos - 1;
	ctl.head.eof = ctl.valid;
finished:
	if (ctl.page) {
		kunmap(ctl.page);
		SetPageUptodate(ctl.page);
		unlock_page(ctl.page);
		put_page(ctl.page);
	}
	if (page) {
		cache->head = ctl.head;
		kunmap(page);
		SetPageUptodate(page);
		unlock_page(page);
		put_page(page);
	}
out:
	return result;
}

static void ncp_d_prune(struct dentry *dentry)
{
	if (!dentry->d_fsdata)	/* not referenced from page cache */
		return;
	NCP_FINFO(d_inode(dentry->d_parent))->flags &= ~NCPI_DIR_CACHE;
}

static int
ncp_fill_cache(struct file *file, struct dir_context *ctx,
		struct ncp_cache_control *ctrl, struct ncp_entry_info *entry,
		int inval_childs)
{
	struct dentry *newdent, *dentry = file->f_path.dentry;
	struct inode *dir = d_inode(dentry);
	struct ncp_cache_control ctl = *ctrl;
	struct qstr qname;
	int valid = 0;
	int hashed = 0;
	ino_t ino = 0;
	__u8 __name[NCP_MAXPATHLEN + 1];

	qname.len = sizeof(__name);
	if (ncp_vol2io(NCP_SERVER(dir), __name, &qname.len,
			entry->i.entryName, entry->i.nameLen,
			!ncp_preserve_entry_case(dir, entry->i.NSCreator)))
		return 1; /* I'm not sure */

	qname.name = __name;

	newdent = d_hash_and_lookup(dentry, &qname);
	if (IS_ERR(newdent))
		goto end_advance;
	if (!newdent) {
		newdent = d_alloc(dentry, &qname);
		if (!newdent)
			goto end_advance;
	} else {
		hashed = 1;

		/* If case sensitivity changed for this volume, all entries below this one
		   should be thrown away.  This entry itself is not affected, as its case
		   sensitivity is controlled by its own parent. */
		if (inval_childs)
			shrink_dcache_parent(newdent);

		/*
		 * NetWare's OS2 namespace is case preserving yet case
		 * insensitive.  So we update dentry's name as received from
		 * server. Parent dir's i_mutex is locked because we're in
		 * readdir.
		 */
		dentry_update_name_case(newdent, &qname);
	}

	if (d_really_is_negative(newdent)) {
		struct inode *inode;

		entry->opened = 0;
		entry->ino = iunique(dir->i_sb, 2);
		inode = ncp_iget(dir->i_sb, entry);
		if (inode) {
			d_instantiate(newdent, inode);
			if (!hashed)
				d_rehash(newdent);
		} else {
			spin_lock(&dentry->d_lock);
			NCP_FINFO(dir)->flags &= ~NCPI_DIR_CACHE;
			spin_unlock(&dentry->d_lock);
		}
	} else {
		struct inode *inode = d_inode(newdent);

		inode_lock_nested(inode, I_MUTEX_CHILD);
		ncp_update_inode2(inode, entry);
		inode_unlock(inode);
	}

	if (ctl.idx >= NCP_DIRCACHE_SIZE) {
		if (ctl.page) {
			kunmap(ctl.page);
			SetPageUptodate(ctl.page);
			unlock_page(ctl.page);
			put_page(ctl.page);
		}
		ctl.cache = NULL;
		ctl.idx  -= NCP_DIRCACHE_SIZE;
		ctl.ofs  += 1;
		ctl.page  = grab_cache_page(&dir->i_data, ctl.ofs);
		if (ctl.page)
			ctl.cache = kmap(ctl.page);
	}
	if (ctl.cache) {
		if (d_really_is_positive(newdent)) {
			newdent->d_fsdata = newdent;
			ctl.cache->dentry[ctl.idx] = newdent;
			ino = d_inode(newdent)->i_ino;
			ncp_new_dentry(newdent);
		}
 		valid = 1;
	}
	dput(newdent);
end_advance:
	if (!valid)
		ctl.valid = 0;
	if (!ctl.filled && (ctl.fpos == ctx->pos)) {
		if (!ino)
			ino = iunique(dir->i_sb, 2);
		ctl.filled = !dir_emit(ctx, qname.name, qname.len,
				     ino, DT_UNKNOWN);
		if (!ctl.filled)
			ctx->pos += 1;
	}
	ctl.fpos += 1;
	ctl.idx  += 1;
	*ctrl = ctl;
	return (ctl.valid || !ctl.filled);
}

static void
ncp_read_volume_list(struct file *file, struct dir_context *ctx,
			struct ncp_cache_control *ctl)
{
	struct inode *inode = file_inode(file);
	struct ncp_server *server = NCP_SERVER(inode);
	struct ncp_volume_info info;
	struct ncp_entry_info entry;
	int i;

	ncp_dbg(1, "pos=%ld\n", (unsigned long)ctx->pos);

	for (i = 0; i < NCP_NUMBER_OF_VOLUMES; i++) {
		int inval_dentry;

		if (ncp_get_volume_info_with_number(server, i, &info) != 0)
			return;
		if (!strlen(info.volume_name))
			continue;

		ncp_dbg(1, "found vol: %s\n", info.volume_name);

		if (ncp_lookup_volume(server, info.volume_name,
					&entry.i)) {
			ncp_dbg(1, "could not lookup vol %s\n",
				info.volume_name);
			continue;
		}
		inval_dentry = ncp_update_known_namespace(server, entry.i.volNumber, NULL);
		entry.volume = entry.i.volNumber;
		if (!ncp_fill_cache(file, ctx, ctl, &entry, inval_dentry))
			return;
	}
}

static void
ncp_do_readdir(struct file *file, struct dir_context *ctx,
						struct ncp_cache_control *ctl)
{
	struct inode *dir = file_inode(file);
	struct ncp_server *server = NCP_SERVER(dir);
	struct nw_search_sequence seq;
	struct ncp_entry_info entry;
	int err;
	void* buf;
	int more;
	size_t bufsize;

	ncp_dbg(1, "%pD2, fpos=%ld\n", file, (unsigned long)ctx->pos);
	ncp_vdbg("init %pD, volnum=%d, dirent=%u\n",
		 file, NCP_FINFO(dir)->volNumber, NCP_FINFO(dir)->dirEntNum);

	err = ncp_initialize_search(server, dir, &seq);
	if (err) {
		ncp_dbg(1, "init failed, err=%d\n", err);
		return;
	}
	/* We MUST NOT use server->buffer_size handshaked with server if we are
	   using UDP, as for UDP server uses max. buffer size determined by
	   MTU, and for TCP server uses hardwired value 65KB (== 66560 bytes). 
	   So we use 128KB, just to be sure, as there is no way how to know
	   this value in advance. */
	bufsize = 131072;
	buf = vmalloc(bufsize);
	if (!buf)
		return;
	do {
		int cnt;
		char* rpl;
		size_t rpls;

		err = ncp_search_for_fileset(server, &seq, &more, &cnt, buf, bufsize, &rpl, &rpls);
		if (err)		/* Error */
			break;
		if (!cnt)		/* prevent endless loop */
			break;
		while (cnt--) {
			size_t onerpl;
			
			if (rpls < offsetof(struct nw_info_struct, entryName))
				break;	/* short packet */
			ncp_extract_file_info(rpl, &entry.i);
			onerpl = offsetof(struct nw_info_struct, entryName) + entry.i.nameLen;
			if (rpls < onerpl)
				break;	/* short packet */
			(void)ncp_obtain_nfs_info(server, &entry.i);
			rpl += onerpl;
			rpls -= onerpl;
			entry.volume = entry.i.volNumber;
			if (!ncp_fill_cache(file, ctx, ctl, &entry, 0))
				break;
		}
	} while (more);
	vfree(buf);
	return;
}

int ncp_conn_logged_in(struct super_block *sb)
{
	struct ncp_server* server = NCP_SBP(sb);
	int result;

	if (ncp_single_volume(server)) {
		int len;
		struct dentry* dent;
		__u32 volNumber;
		__le32 dirEntNum;
		__le32 DosDirNum;
		__u8 __name[NCP_MAXPATHLEN + 1];

		len = sizeof(__name);
		result = ncp_io2vol(server, __name, &len, server->m.mounted_vol,
				    strlen(server->m.mounted_vol), 1);
		if (result)
			goto out;
		result = -ENOENT;
		if (ncp_get_volume_root(server, __name, &volNumber, &dirEntNum, &DosDirNum)) {
			ncp_vdbg("%s not found\n", server->m.mounted_vol);
			goto out;
		}
		dent = sb->s_root;
		if (dent) {
			struct inode* ino = d_inode(dent);
			if (ino) {
				ncp_update_known_namespace(server, volNumber, NULL);
				NCP_FINFO(ino)->volNumber = volNumber;
				NCP_FINFO(ino)->dirEntNum = dirEntNum;
				NCP_FINFO(ino)->DosDirNum = DosDirNum;
				result = 0;
			} else {
				ncp_dbg(1, "d_inode(sb->s_root) == NULL!\n");
			}
		} else {
			ncp_dbg(1, "sb->s_root == NULL!\n");
		}
	} else
		result = 0;

out:
	return result;
}

static struct dentry *ncp_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags)
{
	struct ncp_server *server = NCP_SERVER(dir);
	struct inode *inode = NULL;
	struct ncp_entry_info finfo;
	int error, res, len;
	__u8 __name[NCP_MAXPATHLEN + 1];

	error = -EIO;
	if (!ncp_conn_valid(server))
		goto finished;

	ncp_vdbg("server lookup for %pd2\n", dentry);

	len = sizeof(__name);
	if (ncp_is_server_root(dir)) {
		res = ncp_io2vol(server, __name, &len, dentry->d_name.name,
				 dentry->d_name.len, 1);
		if (!res)
			res = ncp_lookup_volume(server, __name, &(finfo.i));
		if (!res)
			ncp_update_known_namespace(server, finfo.i.volNumber, NULL);
	} else {
		res = ncp_io2vol(server, __name, &len, dentry->d_name.name,
				 dentry->d_name.len, !ncp_preserve_case(dir));
		if (!res)
			res = ncp_obtain_info(server, dir, __name, &(finfo.i));
	}
	ncp_vdbg("looked for %pd2, res=%d\n", dentry, res);
	/*
	 * If we didn't find an entry, make a negative dentry.
	 */
	if (res)
		goto add_entry;

	/*
	 * Create an inode for the entry.
	 */
	finfo.opened = 0;
	finfo.ino = iunique(dir->i_sb, 2);
	finfo.volume = finfo.i.volNumber;
	error = -EACCES;
	inode = ncp_iget(dir->i_sb, &finfo);

	if (inode) {
		ncp_new_dentry(dentry);
add_entry:
		d_add(dentry, inode);
		error = 0;
	}

finished:
	ncp_vdbg("result=%d\n", error);
	return ERR_PTR(error);
}

/*
 * This code is common to create, mkdir, and mknod.
 */
static int ncp_instantiate(struct inode *dir, struct dentry *dentry,
			struct ncp_entry_info *finfo)
{
	struct inode *inode;
	int error = -EINVAL;

	finfo->ino = iunique(dir->i_sb, 2);
	inode = ncp_iget(dir->i_sb, finfo);
	if (!inode)
		goto out_close;
	d_instantiate(dentry,inode);
	error = 0;
out:
	return error;

out_close:
	ncp_vdbg("%pd2 failed, closing file\n", dentry);
	ncp_close_file(NCP_SERVER(dir), finfo->file_handle);
	goto out;
}

int ncp_create_new(struct inode *dir, struct dentry *dentry, umode_t mode,
		   dev_t rdev, __le32 attributes)
{
	struct ncp_server *server = NCP_SERVER(dir);
	struct ncp_entry_info finfo;
	int error, result, len;
	int opmode;
	__u8 __name[NCP_MAXPATHLEN + 1];
	
	ncp_vdbg("creating %pd2, mode=%hx\n", dentry, mode);

	ncp_age_dentry(server, dentry);
	len = sizeof(__name);
	error = ncp_io2vol(server, __name, &len, dentry->d_name.name,
			   dentry->d_name.len, !ncp_preserve_case(dir));
	if (error)
		goto out;

	error = -EACCES;
	
	if (S_ISREG(mode) && 
	    (server->m.flags & NCP_MOUNT_EXTRAS) && 
	    (mode & S_IXUGO))
		attributes |= aSYSTEM | aSHARED;
	
	result = ncp_open_create_file_or_subdir(server, dir, __name,
				OC_MODE_CREATE | OC_MODE_OPEN | OC_MODE_REPLACE,
				attributes, AR_READ | AR_WRITE, &finfo);
	opmode = O_RDWR;
	if (result) {
		result = ncp_open_create_file_or_subdir(server, dir, __name,
				OC_MODE_CREATE | OC_MODE_OPEN | OC_MODE_REPLACE,
				attributes, AR_WRITE, &finfo);
		if (result) {
			if (result == 0x87)
				error = -ENAMETOOLONG;
			else if (result < 0)
				error = result;
			ncp_dbg(1, "%pd2 failed\n", dentry);
			goto out;
		}
		opmode = O_WRONLY;
	}
	finfo.access = opmode;
	if (ncp_is_nfs_extras(server, finfo.volume)) {
		finfo.i.nfs.mode = mode;
		finfo.i.nfs.rdev = new_encode_dev(rdev);
		if (ncp_modify_nfs_info(server, finfo.volume,
					finfo.i.dirEntNum,
					mode, new_encode_dev(rdev)) != 0)
			goto out;
	}

	error = ncp_instantiate(dir, dentry, &finfo);
out:
	return error;
}

static int ncp_create(struct inode *dir, struct dentry *dentry, umode_t mode,
		bool excl)
{
	return ncp_create_new(dir, dentry, mode, 0, 0);
}

static int ncp_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	struct ncp_entry_info finfo;
	struct ncp_server *server = NCP_SERVER(dir);
	int error, len;
	__u8 __name[NCP_MAXPATHLEN + 1];

	ncp_dbg(1, "making %pd2\n", dentry);

	ncp_age_dentry(server, dentry);
	len = sizeof(__name);
	error = ncp_io2vol(server, __name, &len, dentry->d_name.name,
			   dentry->d_name.len, !ncp_preserve_case(dir));
	if (error)
		goto out;

	error = ncp_open_create_file_or_subdir(server, dir, __name,
					   OC_MODE_CREATE, aDIR,
					   cpu_to_le16(0xffff),
					   &finfo);
	if (error == 0) {
		if (ncp_is_nfs_extras(server, finfo.volume)) {
			mode |= S_IFDIR;
			finfo.i.nfs.mode = mode;
			if (ncp_modify_nfs_info(server,
						finfo.volume,
						finfo.i.dirEntNum,
						mode, 0) != 0)
				goto out;
		}
		error = ncp_instantiate(dir, dentry, &finfo);
	} else if (error > 0) {
		error = -EACCES;
	}
out:
	return error;
}

static int ncp_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct ncp_server *server = NCP_SERVER(dir);
	int error, result, len;
	__u8 __name[NCP_MAXPATHLEN + 1];

	ncp_dbg(1, "removing %pd2\n", dentry);

	len = sizeof(__name);
	error = ncp_io2vol(server, __name, &len, dentry->d_name.name,
			   dentry->d_name.len, !ncp_preserve_case(dir));
	if (error)
		goto out;

	result = ncp_del_file_or_subdir(server, dir, __name);
	switch (result) {
		case 0x00:
			error = 0;
			break;
		case 0x85:	/* unauthorized to delete file */
		case 0x8A:	/* unauthorized to delete file */
			error = -EACCES;
			break;
		case 0x8F:
		case 0x90:	/* read only */
			error = -EPERM;
			break;
		case 0x9F:	/* in use by another client */
			error = -EBUSY;
			break;
		case 0xA0:	/* directory not empty */
			error = -ENOTEMPTY;
			break;
		case 0xFF:	/* someone deleted file */
			error = -ENOENT;
			break;
		default:
			error = result < 0 ? result : -EACCES;
			break;
       	}
out:
	return error;
}

static int ncp_unlink(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = d_inode(dentry);
	struct ncp_server *server;
	int error;

	server = NCP_SERVER(dir);
	ncp_dbg(1, "unlinking %pd2\n", dentry);
	
	/*
	 * Check whether to close the file ...
	 */
	if (inode) {
		ncp_vdbg("closing file\n");
		ncp_make_closed(inode);
	}

	error = ncp_del_file_or_subdir2(server, dentry);
#ifdef CONFIG_NCPFS_STRONG
	/* 9C is Invalid path.. It should be 8F, 90 - read only, but
	   it is not :-( */
	if ((error == 0x9C || error == 0x90) && server->m.flags & NCP_MOUNT_STRONG) { /* R/O */
		error = ncp_force_unlink(dir, dentry);
	}
#endif
	switch (error) {
		case 0x00:
			ncp_dbg(1, "removed %pd2\n", dentry);
			break;
		case 0x85:
		case 0x8A:
			error = -EACCES;
			break;
		case 0x8D:	/* some files in use */
		case 0x8E:	/* all files in use */
			error = -EBUSY;
			break;
		case 0x8F:	/* some read only */
		case 0x90:	/* all read only */
		case 0x9C:	/* !!! returned when in-use or read-only by NW4 */
			error = -EPERM;
			break;
		case 0xFF:
			error = -ENOENT;
			break;
		default:
			error = error < 0 ? error : -EACCES;
			break;
	}
	return error;
}

static int ncp_rename(struct inode *old_dir, struct dentry *old_dentry,
		      struct inode *new_dir, struct dentry *new_dentry,
		      unsigned int flags)
{
	struct ncp_server *server = NCP_SERVER(old_dir);
	int error;
	int old_len, new_len;
	__u8 __old_name[NCP_MAXPATHLEN + 1], __new_name[NCP_MAXPATHLEN + 1];

	if (flags)
		return -EINVAL;

	ncp_dbg(1, "%pd2 to %pd2\n", old_dentry, new_dentry);

	ncp_age_dentry(server, old_dentry);
	ncp_age_dentry(server, new_dentry);

	old_len = sizeof(__old_name);
	error = ncp_io2vol(server, __old_name, &old_len,
			   old_dentry->d_name.name, old_dentry->d_name.len,
			   !ncp_preserve_case(old_dir));
	if (error)
		goto out;

	new_len = sizeof(__new_name);
	error = ncp_io2vol(server, __new_name, &new_len,
			   new_dentry->d_name.name, new_dentry->d_name.len,
			   !ncp_preserve_case(new_dir));
	if (error)
		goto out;

	error = ncp_ren_or_mov_file_or_subdir(server, old_dir, __old_name,
						      new_dir, __new_name);
#ifdef CONFIG_NCPFS_STRONG
	if ((error == 0x90 || error == 0x8B || error == -EACCES) &&
			server->m.flags & NCP_MOUNT_STRONG) {	/* RO */
		error = ncp_force_rename(old_dir, old_dentry, __old_name,
					 new_dir, new_dentry, __new_name);
	}
#endif
	switch (error) {
		case 0x00:
			ncp_dbg(1, "renamed %pd -> %pd\n",
				old_dentry, new_dentry);
			ncp_d_prune(old_dentry);
			ncp_d_prune(new_dentry);
			break;
		case 0x9E:
			error = -ENAMETOOLONG;
			break;
		case 0xFF:
			error = -ENOENT;
			break;
		default:
			error = error < 0 ? error : -EACCES;
			break;
	}
out:
	return error;
}

static int ncp_mknod(struct inode * dir, struct dentry *dentry,
		     umode_t mode, dev_t rdev)
{
	if (ncp_is_nfs_extras(NCP_SERVER(dir), NCP_FINFO(dir)->volNumber)) {
		ncp_dbg(1, "mode = 0%ho\n", mode);
		return ncp_create_new(dir, dentry, mode, rdev, 0);
	}
	return -EPERM; /* Strange, but true */
}

/* The following routines are taken directly from msdos-fs */

/* Linear day numbers of the respective 1sts in non-leap years. */

static int day_n[] =
{0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 0, 0, 0, 0};
/* Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec */

static int utc2local(int time)
{
	return time - sys_tz.tz_minuteswest * 60;
}

static int local2utc(int time)
{
	return time + sys_tz.tz_minuteswest * 60;
}

/* Convert a MS-DOS time/date pair to a UNIX date (seconds since 1 1 70). */
int
ncp_date_dos2unix(__le16 t, __le16 d)
{
	unsigned short time = le16_to_cpu(t), date = le16_to_cpu(d);
	int month, year, secs;

	/* first subtract and mask after that... Otherwise, if
	   date == 0, bad things happen */
	month = ((date >> 5) - 1) & 15;
	year = date >> 9;
	secs = (time & 31) * 2 + 60 * ((time >> 5) & 63) + (time >> 11) * 3600 +
		86400 * ((date & 31) - 1 + day_n[month] + (year / 4) + 
		year * 365 - ((year & 3) == 0 && month < 2 ? 1 : 0) + 3653);
	/* days since 1.1.70 plus 80's leap day */
	return local2utc(secs);
}


/* Convert linear UNIX date to a MS-DOS time/date pair. */
void
ncp_date_unix2dos(int unix_date, __le16 *time, __le16 *date)
{
	int day, year, nl_day, month;

	unix_date = utc2local(unix_date);
	*time = cpu_to_le16(
		(unix_date % 60) / 2 + (((unix_date / 60) % 60) << 5) +
		(((unix_date / 3600) % 24) << 11));
	day = unix_date / 86400 - 3652;
	year = day / 365;
	if ((year + 3) / 4 + 365 * year > day)
		year--;
	day -= (year + 3) / 4 + 365 * year;
	if (day == 59 && !(year & 3)) {
		nl_day = day;
		month = 2;
	} else {
		nl_day = (year & 3) || day <= 59 ? day : day - 1;
		for (month = 1; month < 12; month++)
			if (day_n[month] > nl_day)
				break;
	}
	*date = cpu_to_le16(nl_day - day_n[month - 1] + 1 + (month << 5) + (year << 9));
}
