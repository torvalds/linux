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

#include <linux/config.h>

#include <linux/time.h>
#include <linux/errno.h>
#include <linux/stat.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <asm/uaccess.h>
#include <asm/byteorder.h>
#include <linux/smp_lock.h>

#include <linux/ncp_fs.h>

#include "ncplib_kernel.h"

static void ncp_read_volume_list(struct file *, void *, filldir_t,
				struct ncp_cache_control *);
static void ncp_do_readdir(struct file *, void *, filldir_t,
				struct ncp_cache_control *);

static int ncp_readdir(struct file *, void *, filldir_t);

static int ncp_create(struct inode *, struct dentry *, int, struct nameidata *);
static struct dentry *ncp_lookup(struct inode *, struct dentry *, struct nameidata *);
static int ncp_unlink(struct inode *, struct dentry *);
static int ncp_mkdir(struct inode *, struct dentry *, int);
static int ncp_rmdir(struct inode *, struct dentry *);
static int ncp_rename(struct inode *, struct dentry *,
	  	      struct inode *, struct dentry *);
static int ncp_mknod(struct inode * dir, struct dentry *dentry,
		     int mode, dev_t rdev);
#if defined(CONFIG_NCPFS_EXTRAS) || defined(CONFIG_NCPFS_NFS_NS)
extern int ncp_symlink(struct inode *, struct dentry *, const char *);
#else
#define ncp_symlink NULL
#endif
		      
struct file_operations ncp_dir_operations =
{
	.read		= generic_read_dir,
	.readdir	= ncp_readdir,
	.ioctl		= ncp_ioctl,
};

struct inode_operations ncp_dir_inode_operations =
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
static int ncp_lookup_validate(struct dentry *, struct nameidata *);
static int ncp_hash_dentry(struct dentry *, struct qstr *);
static int ncp_compare_dentry (struct dentry *, struct qstr *, struct qstr *);
static int ncp_delete_dentry(struct dentry *);

static struct dentry_operations ncp_dentry_operations =
{
	.d_revalidate	= ncp_lookup_validate,
	.d_hash		= ncp_hash_dentry,
	.d_compare	= ncp_compare_dentry,
	.d_delete	= ncp_delete_dentry,
};

struct dentry_operations ncp_root_dentry_operations =
{
	.d_hash		= ncp_hash_dentry,
	.d_compare	= ncp_compare_dentry,
	.d_delete	= ncp_delete_dentry,
};


/*
 * Note: leave the hash unchanged if the directory
 * is case-sensitive.
 */
static int 
ncp_hash_dentry(struct dentry *dentry, struct qstr *this)
{
	struct nls_table *t;
	unsigned long hash;
	int i;

	t = NCP_IO_TABLE(dentry);

	if (!ncp_case_sensitive(dentry->d_inode)) {
		hash = init_name_hash();
		for (i=0; i<this->len ; i++)
			hash = partial_name_hash(ncp_tolower(t, this->name[i]),
									hash);
		this->hash = end_name_hash(hash);
	}
	return 0;
}

static int
ncp_compare_dentry(struct dentry *dentry, struct qstr *a, struct qstr *b)
{
	if (a->len != b->len)
		return 1;

	if (ncp_case_sensitive(dentry->d_inode))
		return strncmp(a->name, b->name, a->len);

	return ncp_strnicmp(NCP_IO_TABLE(dentry), a->name, b->name, a->len);
}

/*
 * This is the callback from dput() when d_count is going to 0.
 * We use this to unhash dentries with bad inodes.
 * Closing files can be safely postponed until iput() - it's done there anyway.
 */
static int
ncp_delete_dentry(struct dentry * dentry)
{
	struct inode *inode = dentry->d_inode;

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
	return (!ncp_single_volume(NCP_SERVER(inode)) &&
		inode == inode->i_sb->s_root->d_inode);
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
	inode = dentry->d_inode;

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
	struct inode *old_inode = old_dentry->d_inode;
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
	if (new_dentry && new_dentry->d_inode) {
		new_nwattr = NCP_FINFO(new_dentry->d_inode)->nwattr;
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
__ncp_lookup_validate(struct dentry * dentry, struct nameidata *nd)
{
	struct ncp_server *server;
	struct dentry *parent;
	struct inode *dir;
	struct ncp_entry_info finfo;
	int res, val = 0, len;
	__u8 __name[NCP_MAXPATHLEN + 1];

	parent = dget_parent(dentry);
	dir = parent->d_inode;

	if (!dentry->d_inode)
		goto finished;

	server = NCP_SERVER(dir);

	if (!ncp_conn_valid(server))
		goto finished;

	/*
	 * Inspired by smbfs:
	 * The default validation is based on dentry age:
	 * We set the max age at mount time.  (But each
	 * successful server lookup renews the timestamp.)
	 */
	val = NCP_TEST_AGE(server, dentry);
	if (val)
		goto finished;

	DDPRINTK("ncp_lookup_validate: %s/%s not valid, age=%ld, server lookup\n",
		dentry->d_parent->d_name.name, dentry->d_name.name,
		NCP_GET_AGE(dentry));

	len = sizeof(__name);
	if (ncp_is_server_root(dir)) {
		res = ncp_io2vol(server, __name, &len, dentry->d_name.name,
				 dentry->d_name.len, 1);
		if (!res)
			res = ncp_lookup_volume(server, __name, &(finfo.i));
	} else {
		res = ncp_io2vol(server, __name, &len, dentry->d_name.name,
				 dentry->d_name.len, !ncp_preserve_case(dir));
		if (!res)
			res = ncp_obtain_info(server, dir, __name, &(finfo.i));
	}
	finfo.volume = finfo.i.volNumber;
	DDPRINTK("ncp_lookup_validate: looked for %s/%s, res=%d\n",
		dentry->d_parent->d_name.name, __name, res);
	/*
	 * If we didn't find it, or if it has a different dirEntNum to
	 * what we remember, it's not valid any more.
	 */
	if (!res) {
		if (finfo.i.dirEntNum == NCP_FINFO(dentry->d_inode)->dirEntNum) {
			ncp_new_dentry(dentry);
			val=1;
		} else
			DDPRINTK("ncp_lookup_validate: found, but dirEntNum changed\n");

		ncp_update_inode2(dentry->d_inode, &finfo);
	}

finished:
	DDPRINTK("ncp_lookup_validate: result=%d\n", val);
	dput(parent);
	return val;
}

static int
ncp_lookup_validate(struct dentry * dentry, struct nameidata *nd)
{
	int res;
	lock_kernel();
	res = __ncp_lookup_validate(dentry, nd);
	unlock_kernel();
	return res;
}

static struct dentry *
ncp_dget_fpos(struct dentry *dentry, struct dentry *parent, unsigned long fpos)
{
	struct dentry *dent = dentry;
	struct list_head *next;

	if (d_validate(dent, parent)) {
		if (dent->d_name.len <= NCP_MAXPATHLEN &&
		    (unsigned long)dent->d_fsdata == fpos) {
			if (!dent->d_inode) {
				dput(dent);
				dent = NULL;
			}
			return dent;
		}
		dput(dent);
	}

	/* If a pointer is invalid, we search the dentry. */
	spin_lock(&dcache_lock);
	next = parent->d_subdirs.next;
	while (next != &parent->d_subdirs) {
		dent = list_entry(next, struct dentry, d_child);
		if ((unsigned long)dent->d_fsdata == fpos) {
			if (dent->d_inode)
				dget_locked(dent);
			else
				dent = NULL;
			spin_unlock(&dcache_lock);
			goto out;
		}
		next = next->next;
	}
	spin_unlock(&dcache_lock);
	return NULL;

out:
	return dent;
}

static time_t ncp_obtain_mtime(struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	struct ncp_server *server = NCP_SERVER(inode);
	struct nw_info_struct i;

	if (!ncp_conn_valid(server) || ncp_is_server_root(inode))
		return 0;

	if (ncp_obtain_info(server, inode, NULL, &i))
		return 0;

	return ncp_date_dos2unix(i.modifyTime, i.modifyDate);
}

static int ncp_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	struct dentry *dentry = filp->f_dentry;
	struct inode *inode = dentry->d_inode;
	struct page *page = NULL;
	struct ncp_server *server = NCP_SERVER(inode);
	union  ncp_dir_cache *cache = NULL;
	struct ncp_cache_control ctl;
	int result, mtime_valid = 0;
	time_t mtime = 0;

	lock_kernel();

	ctl.page  = NULL;
	ctl.cache = NULL;

	DDPRINTK("ncp_readdir: reading %s/%s, pos=%d\n",
		dentry->d_parent->d_name.name, dentry->d_name.name,
		(int) filp->f_pos);

	result = -EIO;
	if (!ncp_conn_valid(server))
		goto out;

	result = 0;
	if (filp->f_pos == 0) {
		if (filldir(dirent, ".", 1, 0, inode->i_ino, DT_DIR))
			goto out;
		filp->f_pos = 1;
	}
	if (filp->f_pos == 1) {
		if (filldir(dirent, "..", 2, 1, parent_ino(dentry), DT_DIR))
			goto out;
		filp->f_pos = 2;
	}

	page = grab_cache_page(&inode->i_data, 0);
	if (!page)
		goto read_really;

	ctl.cache = cache = kmap(page);
	ctl.head  = cache->head;

	if (!PageUptodate(page) || !ctl.head.eof)
		goto init_cache;

	if (filp->f_pos == 2) {
		if (jiffies - ctl.head.time >= NCP_MAX_AGE(server))
			goto init_cache;

		mtime = ncp_obtain_mtime(dentry);
		mtime_valid = 1;
		if ((!mtime) || (mtime != ctl.head.mtime))
			goto init_cache;
	}

	if (filp->f_pos > ctl.head.end)
		goto finished;

	ctl.fpos = filp->f_pos + (NCP_DIRCACHE_START - 2);
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
			int res;

			dent = ncp_dget_fpos(ctl.cache->dentry[ctl.idx],
						dentry, filp->f_pos);
			if (!dent)
				goto invalid_cache;
			res = filldir(dirent, dent->d_name.name,
					dent->d_name.len, filp->f_pos,
					dent->d_inode->i_ino, DT_UNKNOWN);
			dput(dent);
			if (res)
				goto finished;
			filp->f_pos += 1;
			ctl.idx += 1;
			if (filp->f_pos > ctl.head.end)
				goto finished;
		}
		if (ctl.page) {
			kunmap(ctl.page);
			SetPageUptodate(ctl.page);
			unlock_page(ctl.page);
			page_cache_release(ctl.page);
			ctl.page = NULL;
		}
		ctl.idx  = 0;
		ctl.ofs += 1;
	}
invalid_cache:
	if (ctl.page) {
		kunmap(ctl.page);
		unlock_page(ctl.page);
		page_cache_release(ctl.page);
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
	if (ncp_is_server_root(inode)) {
		ncp_read_volume_list(filp, dirent, filldir, &ctl);
	} else {
		ncp_do_readdir(filp, dirent, filldir, &ctl);
	}
	ctl.head.end = ctl.fpos - 1;
	ctl.head.eof = ctl.valid;
finished:
	if (page) {
		cache->head = ctl.head;
		kunmap(page);
		SetPageUptodate(page);
		unlock_page(page);
		page_cache_release(page);
	}
	if (ctl.page) {
		kunmap(ctl.page);
		SetPageUptodate(ctl.page);
		unlock_page(ctl.page);
		page_cache_release(ctl.page);
	}
out:
	unlock_kernel();
	return result;
}

static int
ncp_fill_cache(struct file *filp, void *dirent, filldir_t filldir,
		struct ncp_cache_control *ctrl, struct ncp_entry_info *entry)
{
	struct dentry *newdent, *dentry = filp->f_dentry;
	struct inode *newino, *inode = dentry->d_inode;
	struct ncp_cache_control ctl = *ctrl;
	struct qstr qname;
	int valid = 0;
	int hashed = 0;
	ino_t ino = 0;
	__u8 __name[NCP_MAXPATHLEN + 1];

	qname.len = sizeof(__name);
	if (ncp_vol2io(NCP_SERVER(inode), __name, &qname.len,
			entry->i.entryName, entry->i.nameLen,
			!ncp_preserve_entry_case(inode, entry->i.NSCreator)))
		return 1; /* I'm not sure */

	qname.name = __name;
	qname.hash = full_name_hash(qname.name, qname.len);

	if (dentry->d_op && dentry->d_op->d_hash)
		if (dentry->d_op->d_hash(dentry, &qname) != 0)
			goto end_advance;

	newdent = d_lookup(dentry, &qname);

	if (!newdent) {
		newdent = d_alloc(dentry, &qname);
		if (!newdent)
			goto end_advance;
	} else {
		hashed = 1;
		memcpy((char *) newdent->d_name.name, qname.name,
							newdent->d_name.len);
	}

	if (!newdent->d_inode) {
		entry->opened = 0;
		entry->ino = iunique(inode->i_sb, 2);
		newino = ncp_iget(inode->i_sb, entry);
		if (newino) {
			newdent->d_op = &ncp_dentry_operations;
			d_instantiate(newdent, newino);
			if (!hashed)
				d_rehash(newdent);
		}
	} else
		ncp_update_inode2(newdent->d_inode, entry);

	if (newdent->d_inode) {
		ino = newdent->d_inode->i_ino;
		newdent->d_fsdata = (void *) ctl.fpos;
		ncp_new_dentry(newdent);
	}

	if (ctl.idx >= NCP_DIRCACHE_SIZE) {
		if (ctl.page) {
			kunmap(ctl.page);
			SetPageUptodate(ctl.page);
			unlock_page(ctl.page);
			page_cache_release(ctl.page);
		}
		ctl.cache = NULL;
		ctl.idx  -= NCP_DIRCACHE_SIZE;
		ctl.ofs  += 1;
		ctl.page  = grab_cache_page(&inode->i_data, ctl.ofs);
		if (ctl.page)
			ctl.cache = kmap(ctl.page);
	}
	if (ctl.cache) {
		ctl.cache->dentry[ctl.idx] = newdent;
		valid = 1;
	}
	dput(newdent);
end_advance:
	if (!valid)
		ctl.valid = 0;
	if (!ctl.filled && (ctl.fpos == filp->f_pos)) {
		if (!ino)
			ino = find_inode_number(dentry, &qname);
		if (!ino)
			ino = iunique(inode->i_sb, 2);
		ctl.filled = filldir(dirent, qname.name, qname.len,
				     filp->f_pos, ino, DT_UNKNOWN);
		if (!ctl.filled)
			filp->f_pos += 1;
	}
	ctl.fpos += 1;
	ctl.idx  += 1;
	*ctrl = ctl;
	return (ctl.valid || !ctl.filled);
}

static void
ncp_read_volume_list(struct file *filp, void *dirent, filldir_t filldir,
			struct ncp_cache_control *ctl)
{
	struct dentry *dentry = filp->f_dentry;
	struct inode *inode = dentry->d_inode;
	struct ncp_server *server = NCP_SERVER(inode);
	struct ncp_volume_info info;
	struct ncp_entry_info entry;
	int i;

	DPRINTK("ncp_read_volume_list: pos=%ld\n",
			(unsigned long) filp->f_pos);

	for (i = 0; i < NCP_NUMBER_OF_VOLUMES; i++) {

		if (ncp_get_volume_info_with_number(server, i, &info) != 0)
			return;
		if (!strlen(info.volume_name))
			continue;

		DPRINTK("ncp_read_volume_list: found vol: %s\n",
			info.volume_name);

		if (ncp_lookup_volume(server, info.volume_name,
					&entry.i)) {
			DPRINTK("ncpfs: could not lookup vol %s\n",
				info.volume_name);
			continue;
		}
		entry.volume = entry.i.volNumber;
		if (!ncp_fill_cache(filp, dirent, filldir, ctl, &entry))
			return;
	}
}

static void
ncp_do_readdir(struct file *filp, void *dirent, filldir_t filldir,
						struct ncp_cache_control *ctl)
{
	struct dentry *dentry = filp->f_dentry;
	struct inode *dir = dentry->d_inode;
	struct ncp_server *server = NCP_SERVER(dir);
	struct nw_search_sequence seq;
	struct ncp_entry_info entry;
	int err;
	void* buf;
	int more;
	size_t bufsize;

	DPRINTK("ncp_do_readdir: %s/%s, fpos=%ld\n",
		dentry->d_parent->d_name.name, dentry->d_name.name,
		(unsigned long) filp->f_pos);
	PPRINTK("ncp_do_readdir: init %s, volnum=%d, dirent=%u\n",
		dentry->d_name.name, NCP_FINFO(dir)->volNumber,
		NCP_FINFO(dir)->dirEntNum);

	err = ncp_initialize_search(server, dir, &seq);
	if (err) {
		DPRINTK("ncp_do_readdir: init failed, err=%d\n", err);
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
			if (!ncp_fill_cache(filp, dirent, filldir, ctl, &entry))
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
			PPRINTK("ncp_conn_logged_in: %s not found\n",
				server->m.mounted_vol);
			goto out;
		}
		dent = sb->s_root;
		if (dent) {
			struct inode* ino = dent->d_inode;
			if (ino) {
				NCP_FINFO(ino)->volNumber = volNumber;
				NCP_FINFO(ino)->dirEntNum = dirEntNum;
				NCP_FINFO(ino)->DosDirNum = DosDirNum;
			} else {
				DPRINTK("ncpfs: sb->s_root->d_inode == NULL!\n");
			}
		} else {
			DPRINTK("ncpfs: sb->s_root == NULL!\n");
		}
	}
	result = 0;

out:
	return result;
}

static struct dentry *ncp_lookup(struct inode *dir, struct dentry *dentry, struct nameidata *nd)
{
	struct ncp_server *server = NCP_SERVER(dir);
	struct inode *inode = NULL;
	struct ncp_entry_info finfo;
	int error, res, len;
	__u8 __name[NCP_MAXPATHLEN + 1];

	lock_kernel();
	error = -EIO;
	if (!ncp_conn_valid(server))
		goto finished;

	PPRINTK("ncp_lookup: server lookup for %s/%s\n",
		dentry->d_parent->d_name.name, dentry->d_name.name);

	len = sizeof(__name);
	if (ncp_is_server_root(dir)) {
		res = ncp_io2vol(server, __name, &len, dentry->d_name.name,
				 dentry->d_name.len, 1);
		if (!res)
			res = ncp_lookup_volume(server, __name, &(finfo.i));
	} else {
		res = ncp_io2vol(server, __name, &len, dentry->d_name.name,
				 dentry->d_name.len, !ncp_preserve_case(dir));
		if (!res)
			res = ncp_obtain_info(server, dir, __name, &(finfo.i));
	}
	PPRINTK("ncp_lookup: looked for %s/%s, res=%d\n",
		dentry->d_parent->d_name.name, __name, res);
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
		dentry->d_op = &ncp_dentry_operations;
		d_add(dentry, inode);
		error = 0;
	}

finished:
	PPRINTK("ncp_lookup: result=%d\n", error);
	unlock_kernel();
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
	PPRINTK("ncp_instantiate: %s/%s failed, closing file\n",
		dentry->d_parent->d_name.name, dentry->d_name.name);
	ncp_close_file(NCP_SERVER(dir), finfo->file_handle);
	goto out;
}

int ncp_create_new(struct inode *dir, struct dentry *dentry, int mode,
		   dev_t rdev, __le32 attributes)
{
	struct ncp_server *server = NCP_SERVER(dir);
	struct ncp_entry_info finfo;
	int error, result, len;
	int opmode;
	__u8 __name[NCP_MAXPATHLEN + 1];
	
	PPRINTK("ncp_create_new: creating %s/%s, mode=%x\n",
		dentry->d_parent->d_name.name, dentry->d_name.name, mode);

	error = -EIO;
	lock_kernel();
	if (!ncp_conn_valid(server))
		goto out;

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
			DPRINTK("ncp_create: %s/%s failed\n",
				dentry->d_parent->d_name.name, dentry->d_name.name);
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
	unlock_kernel();
	return error;
}

static int ncp_create(struct inode *dir, struct dentry *dentry, int mode,
		struct nameidata *nd)
{
	return ncp_create_new(dir, dentry, mode, 0, 0);
}

static int ncp_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	struct ncp_entry_info finfo;
	struct ncp_server *server = NCP_SERVER(dir);
	int error, len;
	__u8 __name[NCP_MAXPATHLEN + 1];

	DPRINTK("ncp_mkdir: making %s/%s\n",
		dentry->d_parent->d_name.name, dentry->d_name.name);

	error = -EIO;
	lock_kernel();
	if (!ncp_conn_valid(server))
		goto out;

	ncp_age_dentry(server, dentry);
	len = sizeof(__name);
	error = ncp_io2vol(server, __name, &len, dentry->d_name.name,
			   dentry->d_name.len, !ncp_preserve_case(dir));
	if (error)
		goto out;

	error = -EACCES;
	if (ncp_open_create_file_or_subdir(server, dir, __name,
					   OC_MODE_CREATE, aDIR,
					   cpu_to_le16(0xffff),
					   &finfo) == 0)
	{
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
	}
out:
	unlock_kernel();
	return error;
}

static int ncp_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct ncp_server *server = NCP_SERVER(dir);
	int error, result, len;
	__u8 __name[NCP_MAXPATHLEN + 1];

	DPRINTK("ncp_rmdir: removing %s/%s\n",
		dentry->d_parent->d_name.name, dentry->d_name.name);

	error = -EIO;
	lock_kernel();
	if (!ncp_conn_valid(server))
		goto out;

	error = -EBUSY;
	if (!d_unhashed(dentry))
		goto out;

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
			error = -EACCES;
			break;
       	}
out:
	unlock_kernel();
	return error;
}

static int ncp_unlink(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	struct ncp_server *server;
	int error;

	lock_kernel();
	server = NCP_SERVER(dir);
	DPRINTK("ncp_unlink: unlinking %s/%s\n",
		dentry->d_parent->d_name.name, dentry->d_name.name);
	
	error = -EIO;
	if (!ncp_conn_valid(server))
		goto out;

	/*
	 * Check whether to close the file ...
	 */
	if (inode) {
		PPRINTK("ncp_unlink: closing file\n");
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
			DPRINTK("ncp: removed %s/%s\n",
				dentry->d_parent->d_name.name, dentry->d_name.name);
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
			error = -EACCES;
			break;
	}
		
out:
	unlock_kernel();
	return error;
}

static int ncp_rename(struct inode *old_dir, struct dentry *old_dentry,
		      struct inode *new_dir, struct dentry *new_dentry)
{
	struct ncp_server *server = NCP_SERVER(old_dir);
	int error;
	int old_len, new_len;
	__u8 __old_name[NCP_MAXPATHLEN + 1], __new_name[NCP_MAXPATHLEN + 1];

	DPRINTK("ncp_rename: %s/%s to %s/%s\n",
		old_dentry->d_parent->d_name.name, old_dentry->d_name.name,
		new_dentry->d_parent->d_name.name, new_dentry->d_name.name);

	error = -EIO;
	lock_kernel();
	if (!ncp_conn_valid(server))
		goto out;

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
               	        DPRINTK("ncp renamed %s -> %s.\n",
                                old_dentry->d_name.name,new_dentry->d_name.name);
			break;
		case 0x9E:
			error = -ENAMETOOLONG;
			break;
		case 0xFF:
			error = -ENOENT;
			break;
		default:
			error = -EACCES;
			break;
	}
out:
	unlock_kernel();
	return error;
}

static int ncp_mknod(struct inode * dir, struct dentry *dentry,
		     int mode, dev_t rdev)
{
	if (!new_valid_dev(rdev))
		return -EINVAL;
	if (ncp_is_nfs_extras(NCP_SERVER(dir), NCP_FINFO(dir)->volNumber)) {
		DPRINTK(KERN_DEBUG "ncp_mknod: mode = 0%o\n", mode);
		return ncp_create_new(dir, dentry, mode, rdev, 0);
	}
	return -EPERM; /* Strange, but true */
}

/* The following routines are taken directly from msdos-fs */

/* Linear day numbers of the respective 1sts in non-leap years. */

static int day_n[] =
{0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 0, 0, 0, 0};
/* Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec */


extern struct timezone sys_tz;

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
		for (month = 0; month < 12; month++)
			if (day_n[month] > nl_day)
				break;
	}
	*date = cpu_to_le16(nl_day - day_n[month - 1] + 1 + (month << 5) + (year << 9));
}
