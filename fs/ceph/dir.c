#include <linux/ceph/ceph_debug.h>

#include <linux/spinlock.h>
#include <linux/fs_struct.h>
#include <linux/namei.h>
#include <linux/slab.h>
#include <linux/sched.h>

#include "super.h"
#include "mds_client.h"

/*
 * Directory operations: readdir, lookup, create, link, unlink,
 * rename, etc.
 */

/*
 * Ceph MDS operations are specified in terms of a base ino and
 * relative path.  Thus, the client can specify an operation on a
 * specific inode (e.g., a getattr due to fstat(2)), or as a path
 * relative to, say, the root directory.
 *
 * Normally, we limit ourselves to strict inode ops (no path component)
 * or dentry operations (a single path component relative to an ino).  The
 * exception to this is open_root_dentry(), which will open the mount
 * point by name.
 */

const struct inode_operations ceph_dir_iops;
const struct file_operations ceph_dir_fops;
const struct dentry_operations ceph_dentry_ops;

/*
 * Initialize ceph dentry state.
 */
int ceph_init_dentry(struct dentry *dentry)
{
	struct ceph_dentry_info *di;

	if (dentry->d_fsdata)
		return 0;

	if (dentry->d_parent == NULL ||   /* nfs fh_to_dentry */
	    ceph_snap(dentry->d_parent->d_inode) == CEPH_NOSNAP)
		d_set_d_op(dentry, &ceph_dentry_ops);
	else if (ceph_snap(dentry->d_parent->d_inode) == CEPH_SNAPDIR)
		d_set_d_op(dentry, &ceph_snapdir_dentry_ops);
	else
		d_set_d_op(dentry, &ceph_snap_dentry_ops);

	di = kmem_cache_alloc(ceph_dentry_cachep, GFP_NOFS | __GFP_ZERO);
	if (!di)
		return -ENOMEM;          /* oh well */

	spin_lock(&dentry->d_lock);
	if (dentry->d_fsdata) {
		/* lost a race */
		kmem_cache_free(ceph_dentry_cachep, di);
		goto out_unlock;
	}
	di->dentry = dentry;
	di->lease_session = NULL;
	dentry->d_fsdata = di;
	dentry->d_time = jiffies;
	ceph_dentry_lru_add(dentry);
out_unlock:
	spin_unlock(&dentry->d_lock);
	return 0;
}



/*
 * for readdir, we encode the directory frag and offset within that
 * frag into f_pos.
 */
static unsigned fpos_frag(loff_t p)
{
	return p >> 32;
}
static unsigned fpos_off(loff_t p)
{
	return p & 0xffffffff;
}

/*
 * When possible, we try to satisfy a readdir by peeking at the
 * dcache.  We make this work by carefully ordering dentries on
 * d_u.d_child when we initially get results back from the MDS, and
 * falling back to a "normal" sync readdir if any dentries in the dir
 * are dropped.
 *
 * I_COMPLETE tells indicates we have all dentries in the dir.  It is
 * defined IFF we hold CEPH_CAP_FILE_SHARED (which will be revoked by
 * the MDS if/when the directory is modified).
 */
static int __dcache_readdir(struct file *filp,
			    void *dirent, filldir_t filldir)
{
	struct ceph_file_info *fi = filp->private_data;
	struct dentry *parent = filp->f_dentry;
	struct inode *dir = parent->d_inode;
	struct list_head *p;
	struct dentry *dentry, *last;
	struct ceph_dentry_info *di;
	int err = 0;

	/* claim ref on last dentry we returned */
	last = fi->dentry;
	fi->dentry = NULL;

	dout("__dcache_readdir %p at %llu (last %p)\n", dir, filp->f_pos,
	     last);

	spin_lock(&parent->d_lock);

	/* start at beginning? */
	if (filp->f_pos == 2 || last == NULL ||
	    filp->f_pos < ceph_dentry(last)->offset) {
		if (list_empty(&parent->d_subdirs))
			goto out_unlock;
		p = parent->d_subdirs.prev;
		dout(" initial p %p/%p\n", p->prev, p->next);
	} else {
		p = last->d_u.d_child.prev;
	}

more:
	dentry = list_entry(p, struct dentry, d_u.d_child);
	di = ceph_dentry(dentry);
	while (1) {
		dout(" p %p/%p %s d_subdirs %p/%p\n", p->prev, p->next,
		     d_unhashed(dentry) ? "!hashed" : "hashed",
		     parent->d_subdirs.prev, parent->d_subdirs.next);
		if (p == &parent->d_subdirs) {
			fi->at_end = 1;
			goto out_unlock;
		}
		spin_lock_nested(&dentry->d_lock, DENTRY_D_LOCK_NESTED);
		if (!d_unhashed(dentry) && dentry->d_inode &&
		    ceph_snap(dentry->d_inode) != CEPH_SNAPDIR &&
		    ceph_ino(dentry->d_inode) != CEPH_INO_CEPH &&
		    filp->f_pos <= di->offset)
			break;
		dout(" skipping %p %.*s at %llu (%llu)%s%s\n", dentry,
		     dentry->d_name.len, dentry->d_name.name, di->offset,
		     filp->f_pos, d_unhashed(dentry) ? " unhashed" : "",
		     !dentry->d_inode ? " null" : "");
		spin_unlock(&dentry->d_lock);
		p = p->prev;
		dentry = list_entry(p, struct dentry, d_u.d_child);
		di = ceph_dentry(dentry);
	}

	dget_dlock(dentry);
	spin_unlock(&dentry->d_lock);
	spin_unlock(&parent->d_lock);

	dout(" %llu (%llu) dentry %p %.*s %p\n", di->offset, filp->f_pos,
	     dentry, dentry->d_name.len, dentry->d_name.name, dentry->d_inode);
	filp->f_pos = di->offset;
	err = filldir(dirent, dentry->d_name.name,
		      dentry->d_name.len, di->offset,
		      dentry->d_inode->i_ino,
		      dentry->d_inode->i_mode >> 12);

	if (last) {
		if (err < 0) {
			/* remember our position */
			fi->dentry = last;
			fi->next_offset = di->offset;
		} else {
			dput(last);
		}
	}
	last = dentry;

	if (err < 0)
		goto out;

	filp->f_pos++;

	/* make sure a dentry wasn't dropped while we didn't have parent lock */
	if (!ceph_i_test(dir, CEPH_I_COMPLETE)) {
		dout(" lost I_COMPLETE on %p; falling back to mds\n", dir);
		err = -EAGAIN;
		goto out;
	}

	spin_lock(&parent->d_lock);
	p = p->prev;	/* advance to next dentry */
	goto more;

out_unlock:
	spin_unlock(&parent->d_lock);
out:
	if (last)
		dput(last);
	return err;
}

/*
 * make note of the last dentry we read, so we can
 * continue at the same lexicographical point,
 * regardless of what dir changes take place on the
 * server.
 */
static int note_last_dentry(struct ceph_file_info *fi, const char *name,
			    int len)
{
	kfree(fi->last_name);
	fi->last_name = kmalloc(len+1, GFP_NOFS);
	if (!fi->last_name)
		return -ENOMEM;
	memcpy(fi->last_name, name, len);
	fi->last_name[len] = 0;
	dout("note_last_dentry '%s'\n", fi->last_name);
	return 0;
}

static int ceph_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	struct ceph_file_info *fi = filp->private_data;
	struct inode *inode = filp->f_dentry->d_inode;
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_fs_client *fsc = ceph_inode_to_client(inode);
	struct ceph_mds_client *mdsc = fsc->mdsc;
	unsigned frag = fpos_frag(filp->f_pos);
	int off = fpos_off(filp->f_pos);
	int err;
	u32 ftype;
	struct ceph_mds_reply_info_parsed *rinfo;
	const int max_entries = fsc->mount_options->max_readdir;
	const int max_bytes = fsc->mount_options->max_readdir_bytes;

	dout("readdir %p filp %p frag %u off %u\n", inode, filp, frag, off);
	if (fi->at_end)
		return 0;

	/* always start with . and .. */
	if (filp->f_pos == 0) {
		/* note dir version at start of readdir so we can tell
		 * if any dentries get dropped */
		fi->dir_release_count = ci->i_release_count;

		dout("readdir off 0 -> '.'\n");
		if (filldir(dirent, ".", 1, ceph_make_fpos(0, 0),
			    inode->i_ino, inode->i_mode >> 12) < 0)
			return 0;
		filp->f_pos = 1;
		off = 1;
	}
	if (filp->f_pos == 1) {
		dout("readdir off 1 -> '..'\n");
		if (filldir(dirent, "..", 2, ceph_make_fpos(0, 1),
			    filp->f_dentry->d_parent->d_inode->i_ino,
			    inode->i_mode >> 12) < 0)
			return 0;
		filp->f_pos = 2;
		off = 2;
	}

	/* can we use the dcache? */
	spin_lock(&inode->i_lock);
	if ((filp->f_pos == 2 || fi->dentry) &&
	    !ceph_test_mount_opt(fsc, NOASYNCREADDIR) &&
	    ceph_snap(inode) != CEPH_SNAPDIR &&
	    (ci->i_ceph_flags & CEPH_I_COMPLETE) &&
	    __ceph_caps_issued_mask(ci, CEPH_CAP_FILE_SHARED, 1)) {
		spin_unlock(&inode->i_lock);
		err = __dcache_readdir(filp, dirent, filldir);
		if (err != -EAGAIN)
			return err;
	} else {
		spin_unlock(&inode->i_lock);
	}
	if (fi->dentry) {
		err = note_last_dentry(fi, fi->dentry->d_name.name,
				       fi->dentry->d_name.len);
		if (err)
			return err;
		dput(fi->dentry);
		fi->dentry = NULL;
	}

	/* proceed with a normal readdir */

more:
	/* do we have the correct frag content buffered? */
	if (fi->frag != frag || fi->last_readdir == NULL) {
		struct ceph_mds_request *req;
		int op = ceph_snap(inode) == CEPH_SNAPDIR ?
			CEPH_MDS_OP_LSSNAP : CEPH_MDS_OP_READDIR;

		/* discard old result, if any */
		if (fi->last_readdir) {
			ceph_mdsc_put_request(fi->last_readdir);
			fi->last_readdir = NULL;
		}

		/* requery frag tree, as the frag topology may have changed */
		frag = ceph_choose_frag(ceph_inode(inode), frag, NULL, NULL);

		dout("readdir fetching %llx.%llx frag %x offset '%s'\n",
		     ceph_vinop(inode), frag, fi->last_name);
		req = ceph_mdsc_create_request(mdsc, op, USE_AUTH_MDS);
		if (IS_ERR(req))
			return PTR_ERR(req);
		req->r_inode = igrab(inode);
		req->r_dentry = dget(filp->f_dentry);
		/* hints to request -> mds selection code */
		req->r_direct_mode = USE_AUTH_MDS;
		req->r_direct_hash = ceph_frag_value(frag);
		req->r_direct_is_hash = true;
		req->r_path2 = kstrdup(fi->last_name, GFP_NOFS);
		req->r_readdir_offset = fi->next_offset;
		req->r_args.readdir.frag = cpu_to_le32(frag);
		req->r_args.readdir.max_entries = cpu_to_le32(max_entries);
		req->r_args.readdir.max_bytes = cpu_to_le32(max_bytes);
		req->r_num_caps = max_entries + 1;
		err = ceph_mdsc_do_request(mdsc, NULL, req);
		if (err < 0) {
			ceph_mdsc_put_request(req);
			return err;
		}
		dout("readdir got and parsed readdir result=%d"
		     " on frag %x, end=%d, complete=%d\n", err, frag,
		     (int)req->r_reply_info.dir_end,
		     (int)req->r_reply_info.dir_complete);

		if (!req->r_did_prepopulate) {
			dout("readdir !did_prepopulate");
			fi->dir_release_count--;    /* preclude I_COMPLETE */
		}

		/* note next offset and last dentry name */
		fi->offset = fi->next_offset;
		fi->last_readdir = req;

		if (req->r_reply_info.dir_end) {
			kfree(fi->last_name);
			fi->last_name = NULL;
			if (ceph_frag_is_rightmost(frag))
				fi->next_offset = 2;
			else
				fi->next_offset = 0;
		} else {
			rinfo = &req->r_reply_info;
			err = note_last_dentry(fi,
				       rinfo->dir_dname[rinfo->dir_nr-1],
				       rinfo->dir_dname_len[rinfo->dir_nr-1]);
			if (err)
				return err;
			fi->next_offset += rinfo->dir_nr;
		}
	}

	rinfo = &fi->last_readdir->r_reply_info;
	dout("readdir frag %x num %d off %d chunkoff %d\n", frag,
	     rinfo->dir_nr, off, fi->offset);
	while (off - fi->offset >= 0 && off - fi->offset < rinfo->dir_nr) {
		u64 pos = ceph_make_fpos(frag, off);
		struct ceph_mds_reply_inode *in =
			rinfo->dir_in[off - fi->offset].in;
		struct ceph_vino vino;
		ino_t ino;

		dout("readdir off %d (%d/%d) -> %lld '%.*s' %p\n",
		     off, off - fi->offset, rinfo->dir_nr, pos,
		     rinfo->dir_dname_len[off - fi->offset],
		     rinfo->dir_dname[off - fi->offset], in);
		BUG_ON(!in);
		ftype = le32_to_cpu(in->mode) >> 12;
		vino.ino = le64_to_cpu(in->ino);
		vino.snap = le64_to_cpu(in->snapid);
		ino = ceph_vino_to_ino(vino);
		if (filldir(dirent,
			    rinfo->dir_dname[off - fi->offset],
			    rinfo->dir_dname_len[off - fi->offset],
			    pos, ino, ftype) < 0) {
			dout("filldir stopping us...\n");
			return 0;
		}
		off++;
		filp->f_pos = pos + 1;
	}

	if (fi->last_name) {
		ceph_mdsc_put_request(fi->last_readdir);
		fi->last_readdir = NULL;
		goto more;
	}

	/* more frags? */
	if (!ceph_frag_is_rightmost(frag)) {
		frag = ceph_frag_next(frag);
		off = 0;
		filp->f_pos = ceph_make_fpos(frag, off);
		dout("readdir next frag is %x\n", frag);
		goto more;
	}
	fi->at_end = 1;

	/*
	 * if dir_release_count still matches the dir, no dentries
	 * were released during the whole readdir, and we should have
	 * the complete dir contents in our cache.
	 */
	spin_lock(&inode->i_lock);
	if (ci->i_release_count == fi->dir_release_count) {
		dout(" marking %p complete\n", inode);
		ci->i_ceph_flags |= CEPH_I_COMPLETE;
		ci->i_max_offset = filp->f_pos;
	}
	spin_unlock(&inode->i_lock);

	dout("readdir %p filp %p done.\n", inode, filp);
	return 0;
}

static void reset_readdir(struct ceph_file_info *fi)
{
	if (fi->last_readdir) {
		ceph_mdsc_put_request(fi->last_readdir);
		fi->last_readdir = NULL;
	}
	kfree(fi->last_name);
	fi->last_name = NULL;
	fi->next_offset = 2;  /* compensate for . and .. */
	if (fi->dentry) {
		dput(fi->dentry);
		fi->dentry = NULL;
	}
	fi->at_end = 0;
}

static loff_t ceph_dir_llseek(struct file *file, loff_t offset, int origin)
{
	struct ceph_file_info *fi = file->private_data;
	struct inode *inode = file->f_mapping->host;
	loff_t old_offset = offset;
	loff_t retval;

	mutex_lock(&inode->i_mutex);
	switch (origin) {
	case SEEK_END:
		offset += inode->i_size + 2;   /* FIXME */
		break;
	case SEEK_CUR:
		offset += file->f_pos;
	}
	retval = -EINVAL;
	if (offset >= 0 && offset <= inode->i_sb->s_maxbytes) {
		if (offset != file->f_pos) {
			file->f_pos = offset;
			file->f_version = 0;
			fi->at_end = 0;
		}
		retval = offset;

		/*
		 * discard buffered readdir content on seekdir(0), or
		 * seek to new frag, or seek prior to current chunk.
		 */
		if (offset == 0 ||
		    fpos_frag(offset) != fpos_frag(old_offset) ||
		    fpos_off(offset) < fi->offset) {
			dout("dir_llseek dropping %p content\n", file);
			reset_readdir(fi);
		}

		/* bump dir_release_count if we did a forward seek */
		if (offset > old_offset)
			fi->dir_release_count--;
	}
	mutex_unlock(&inode->i_mutex);
	return retval;
}

/*
 * Process result of a lookup/open request.
 *
 * Mainly, make sure we return the final req->r_dentry (if it already
 * existed) in place of the original VFS-provided dentry when they
 * differ.
 *
 * Gracefully handle the case where the MDS replies with -ENOENT and
 * no trace (which it may do, at its discretion, e.g., if it doesn't
 * care to issue a lease on the negative dentry).
 */
struct dentry *ceph_finish_lookup(struct ceph_mds_request *req,
				  struct dentry *dentry, int err)
{
	struct ceph_fs_client *fsc = ceph_sb_to_client(dentry->d_sb);
	struct inode *parent = dentry->d_parent->d_inode;

	/* .snap dir? */
	if (err == -ENOENT &&
	    strcmp(dentry->d_name.name,
		   fsc->mount_options->snapdir_name) == 0) {
		struct inode *inode = ceph_get_snapdir(parent);
		dout("ENOENT on snapdir %p '%.*s', linking to snapdir %p\n",
		     dentry, dentry->d_name.len, dentry->d_name.name, inode);
		BUG_ON(!d_unhashed(dentry));
		d_add(dentry, inode);
		err = 0;
	}

	if (err == -ENOENT) {
		/* no trace? */
		err = 0;
		if (!req->r_reply_info.head->is_dentry) {
			dout("ENOENT and no trace, dentry %p inode %p\n",
			     dentry, dentry->d_inode);
			if (dentry->d_inode) {
				d_drop(dentry);
				err = -ENOENT;
			} else {
				d_add(dentry, NULL);
			}
		}
	}
	if (err)
		dentry = ERR_PTR(err);
	else if (dentry != req->r_dentry)
		dentry = dget(req->r_dentry);   /* we got spliced */
	else
		dentry = NULL;
	return dentry;
}

static int is_root_ceph_dentry(struct inode *inode, struct dentry *dentry)
{
	return ceph_ino(inode) == CEPH_INO_ROOT &&
		strncmp(dentry->d_name.name, ".ceph", 5) == 0;
}

/*
 * Look up a single dir entry.  If there is a lookup intent, inform
 * the MDS so that it gets our 'caps wanted' value in a single op.
 */
static struct dentry *ceph_lookup(struct inode *dir, struct dentry *dentry,
				  struct nameidata *nd)
{
	struct ceph_fs_client *fsc = ceph_sb_to_client(dir->i_sb);
	struct ceph_mds_client *mdsc = fsc->mdsc;
	struct ceph_mds_request *req;
	int op;
	int err;

	dout("lookup %p dentry %p '%.*s'\n",
	     dir, dentry, dentry->d_name.len, dentry->d_name.name);

	if (dentry->d_name.len > NAME_MAX)
		return ERR_PTR(-ENAMETOOLONG);

	err = ceph_init_dentry(dentry);
	if (err < 0)
		return ERR_PTR(err);

	/* open (but not create!) intent? */
	if (nd &&
	    (nd->flags & LOOKUP_OPEN) &&
	    (nd->flags & LOOKUP_CONTINUE) == 0 && /* only open last component */
	    !(nd->intent.open.flags & O_CREAT)) {
		int mode = nd->intent.open.create_mode & ~current->fs->umask;
		return ceph_lookup_open(dir, dentry, nd, mode, 1);
	}

	/* can we conclude ENOENT locally? */
	if (dentry->d_inode == NULL) {
		struct ceph_inode_info *ci = ceph_inode(dir);
		struct ceph_dentry_info *di = ceph_dentry(dentry);

		spin_lock(&dir->i_lock);
		dout(" dir %p flags are %d\n", dir, ci->i_ceph_flags);
		if (strncmp(dentry->d_name.name,
			    fsc->mount_options->snapdir_name,
			    dentry->d_name.len) &&
		    !is_root_ceph_dentry(dir, dentry) &&
		    (ci->i_ceph_flags & CEPH_I_COMPLETE) &&
		    (__ceph_caps_issued_mask(ci, CEPH_CAP_FILE_SHARED, 1))) {
			spin_unlock(&dir->i_lock);
			dout(" dir %p complete, -ENOENT\n", dir);
			d_add(dentry, NULL);
			di->lease_shared_gen = ci->i_shared_gen;
			return NULL;
		}
		spin_unlock(&dir->i_lock);
	}

	op = ceph_snap(dir) == CEPH_SNAPDIR ?
		CEPH_MDS_OP_LOOKUPSNAP : CEPH_MDS_OP_LOOKUP;
	req = ceph_mdsc_create_request(mdsc, op, USE_ANY_MDS);
	if (IS_ERR(req))
		return ERR_CAST(req);
	req->r_dentry = dget(dentry);
	req->r_num_caps = 2;
	/* we only need inode linkage */
	req->r_args.getattr.mask = cpu_to_le32(CEPH_STAT_CAP_INODE);
	req->r_locked_dir = dir;
	err = ceph_mdsc_do_request(mdsc, NULL, req);
	dentry = ceph_finish_lookup(req, dentry, err);
	ceph_mdsc_put_request(req);  /* will dput(dentry) */
	dout("lookup result=%p\n", dentry);
	return dentry;
}

/*
 * If we do a create but get no trace back from the MDS, follow up with
 * a lookup (the VFS expects us to link up the provided dentry).
 */
int ceph_handle_notrace_create(struct inode *dir, struct dentry *dentry)
{
	struct dentry *result = ceph_lookup(dir, dentry, NULL);

	if (result && !IS_ERR(result)) {
		/*
		 * We created the item, then did a lookup, and found
		 * it was already linked to another inode we already
		 * had in our cache (and thus got spliced).  Link our
		 * dentry to that inode, but don't hash it, just in
		 * case the VFS wants to dereference it.
		 */
		BUG_ON(!result->d_inode);
		d_instantiate(dentry, result->d_inode);
		return 0;
	}
	return PTR_ERR(result);
}

static int ceph_mknod(struct inode *dir, struct dentry *dentry,
		      int mode, dev_t rdev)
{
	struct ceph_fs_client *fsc = ceph_sb_to_client(dir->i_sb);
	struct ceph_mds_client *mdsc = fsc->mdsc;
	struct ceph_mds_request *req;
	int err;

	if (ceph_snap(dir) != CEPH_NOSNAP)
		return -EROFS;

	dout("mknod in dir %p dentry %p mode 0%o rdev %d\n",
	     dir, dentry, mode, rdev);
	req = ceph_mdsc_create_request(mdsc, CEPH_MDS_OP_MKNOD, USE_AUTH_MDS);
	if (IS_ERR(req)) {
		d_drop(dentry);
		return PTR_ERR(req);
	}
	req->r_dentry = dget(dentry);
	req->r_num_caps = 2;
	req->r_locked_dir = dir;
	req->r_args.mknod.mode = cpu_to_le32(mode);
	req->r_args.mknod.rdev = cpu_to_le32(rdev);
	req->r_dentry_drop = CEPH_CAP_FILE_SHARED;
	req->r_dentry_unless = CEPH_CAP_FILE_EXCL;
	err = ceph_mdsc_do_request(mdsc, dir, req);
	if (!err && !req->r_reply_info.head->is_dentry)
		err = ceph_handle_notrace_create(dir, dentry);
	ceph_mdsc_put_request(req);
	if (err)
		d_drop(dentry);
	return err;
}

static int ceph_create(struct inode *dir, struct dentry *dentry, int mode,
		       struct nameidata *nd)
{
	dout("create in dir %p dentry %p name '%.*s'\n",
	     dir, dentry, dentry->d_name.len, dentry->d_name.name);

	if (ceph_snap(dir) != CEPH_NOSNAP)
		return -EROFS;

	if (nd) {
		BUG_ON((nd->flags & LOOKUP_OPEN) == 0);
		dentry = ceph_lookup_open(dir, dentry, nd, mode, 0);
		/* hrm, what should i do here if we get aliased? */
		if (IS_ERR(dentry))
			return PTR_ERR(dentry);
		return 0;
	}

	/* fall back to mknod */
	return ceph_mknod(dir, dentry, (mode & ~S_IFMT) | S_IFREG, 0);
}

static int ceph_symlink(struct inode *dir, struct dentry *dentry,
			    const char *dest)
{
	struct ceph_fs_client *fsc = ceph_sb_to_client(dir->i_sb);
	struct ceph_mds_client *mdsc = fsc->mdsc;
	struct ceph_mds_request *req;
	int err;

	if (ceph_snap(dir) != CEPH_NOSNAP)
		return -EROFS;

	dout("symlink in dir %p dentry %p to '%s'\n", dir, dentry, dest);
	req = ceph_mdsc_create_request(mdsc, CEPH_MDS_OP_SYMLINK, USE_AUTH_MDS);
	if (IS_ERR(req)) {
		d_drop(dentry);
		return PTR_ERR(req);
	}
	req->r_dentry = dget(dentry);
	req->r_num_caps = 2;
	req->r_path2 = kstrdup(dest, GFP_NOFS);
	req->r_locked_dir = dir;
	req->r_dentry_drop = CEPH_CAP_FILE_SHARED;
	req->r_dentry_unless = CEPH_CAP_FILE_EXCL;
	err = ceph_mdsc_do_request(mdsc, dir, req);
	if (!err && !req->r_reply_info.head->is_dentry)
		err = ceph_handle_notrace_create(dir, dentry);
	ceph_mdsc_put_request(req);
	if (err)
		d_drop(dentry);
	return err;
}

static int ceph_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	struct ceph_fs_client *fsc = ceph_sb_to_client(dir->i_sb);
	struct ceph_mds_client *mdsc = fsc->mdsc;
	struct ceph_mds_request *req;
	int err = -EROFS;
	int op;

	if (ceph_snap(dir) == CEPH_SNAPDIR) {
		/* mkdir .snap/foo is a MKSNAP */
		op = CEPH_MDS_OP_MKSNAP;
		dout("mksnap dir %p snap '%.*s' dn %p\n", dir,
		     dentry->d_name.len, dentry->d_name.name, dentry);
	} else if (ceph_snap(dir) == CEPH_NOSNAP) {
		dout("mkdir dir %p dn %p mode 0%o\n", dir, dentry, mode);
		op = CEPH_MDS_OP_MKDIR;
	} else {
		goto out;
	}
	req = ceph_mdsc_create_request(mdsc, op, USE_AUTH_MDS);
	if (IS_ERR(req)) {
		err = PTR_ERR(req);
		goto out;
	}

	req->r_dentry = dget(dentry);
	req->r_num_caps = 2;
	req->r_locked_dir = dir;
	req->r_args.mkdir.mode = cpu_to_le32(mode);
	req->r_dentry_drop = CEPH_CAP_FILE_SHARED;
	req->r_dentry_unless = CEPH_CAP_FILE_EXCL;
	err = ceph_mdsc_do_request(mdsc, dir, req);
	if (!err && !req->r_reply_info.head->is_dentry)
		err = ceph_handle_notrace_create(dir, dentry);
	ceph_mdsc_put_request(req);
out:
	if (err < 0)
		d_drop(dentry);
	return err;
}

static int ceph_link(struct dentry *old_dentry, struct inode *dir,
		     struct dentry *dentry)
{
	struct ceph_fs_client *fsc = ceph_sb_to_client(dir->i_sb);
	struct ceph_mds_client *mdsc = fsc->mdsc;
	struct ceph_mds_request *req;
	int err;

	if (ceph_snap(dir) != CEPH_NOSNAP)
		return -EROFS;

	dout("link in dir %p old_dentry %p dentry %p\n", dir,
	     old_dentry, dentry);
	req = ceph_mdsc_create_request(mdsc, CEPH_MDS_OP_LINK, USE_AUTH_MDS);
	if (IS_ERR(req)) {
		d_drop(dentry);
		return PTR_ERR(req);
	}
	req->r_dentry = dget(dentry);
	req->r_num_caps = 2;
	req->r_old_dentry = dget(old_dentry); /* or inode? hrm. */
	req->r_locked_dir = dir;
	req->r_dentry_drop = CEPH_CAP_FILE_SHARED;
	req->r_dentry_unless = CEPH_CAP_FILE_EXCL;
	err = ceph_mdsc_do_request(mdsc, dir, req);
	if (err)
		d_drop(dentry);
	else if (!req->r_reply_info.head->is_dentry)
		d_instantiate(dentry, igrab(old_dentry->d_inode));
	ceph_mdsc_put_request(req);
	return err;
}

/*
 * For a soon-to-be unlinked file, drop the AUTH_RDCACHE caps.  If it
 * looks like the link count will hit 0, drop any other caps (other
 * than PIN) we don't specifically want (due to the file still being
 * open).
 */
static int drop_caps_for_unlink(struct inode *inode)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	int drop = CEPH_CAP_LINK_SHARED | CEPH_CAP_LINK_EXCL;

	spin_lock(&inode->i_lock);
	if (inode->i_nlink == 1) {
		drop |= ~(__ceph_caps_wanted(ci) | CEPH_CAP_PIN);
		ci->i_ceph_flags |= CEPH_I_NODELAY;
	}
	spin_unlock(&inode->i_lock);
	return drop;
}

/*
 * rmdir and unlink are differ only by the metadata op code
 */
static int ceph_unlink(struct inode *dir, struct dentry *dentry)
{
	struct ceph_fs_client *fsc = ceph_sb_to_client(dir->i_sb);
	struct ceph_mds_client *mdsc = fsc->mdsc;
	struct inode *inode = dentry->d_inode;
	struct ceph_mds_request *req;
	int err = -EROFS;
	int op;

	if (ceph_snap(dir) == CEPH_SNAPDIR) {
		/* rmdir .snap/foo is RMSNAP */
		dout("rmsnap dir %p '%.*s' dn %p\n", dir, dentry->d_name.len,
		     dentry->d_name.name, dentry);
		op = CEPH_MDS_OP_RMSNAP;
	} else if (ceph_snap(dir) == CEPH_NOSNAP) {
		dout("unlink/rmdir dir %p dn %p inode %p\n",
		     dir, dentry, inode);
		op = ((dentry->d_inode->i_mode & S_IFMT) == S_IFDIR) ?
			CEPH_MDS_OP_RMDIR : CEPH_MDS_OP_UNLINK;
	} else
		goto out;
	req = ceph_mdsc_create_request(mdsc, op, USE_AUTH_MDS);
	if (IS_ERR(req)) {
		err = PTR_ERR(req);
		goto out;
	}
	req->r_dentry = dget(dentry);
	req->r_num_caps = 2;
	req->r_locked_dir = dir;
	req->r_dentry_drop = CEPH_CAP_FILE_SHARED;
	req->r_dentry_unless = CEPH_CAP_FILE_EXCL;
	req->r_inode_drop = drop_caps_for_unlink(inode);
	err = ceph_mdsc_do_request(mdsc, dir, req);
	if (!err && !req->r_reply_info.head->is_dentry)
		d_delete(dentry);
	ceph_mdsc_put_request(req);
out:
	return err;
}

static int ceph_rename(struct inode *old_dir, struct dentry *old_dentry,
		       struct inode *new_dir, struct dentry *new_dentry)
{
	struct ceph_fs_client *fsc = ceph_sb_to_client(old_dir->i_sb);
	struct ceph_mds_client *mdsc = fsc->mdsc;
	struct ceph_mds_request *req;
	int err;

	if (ceph_snap(old_dir) != ceph_snap(new_dir))
		return -EXDEV;
	if (ceph_snap(old_dir) != CEPH_NOSNAP ||
	    ceph_snap(new_dir) != CEPH_NOSNAP)
		return -EROFS;
	dout("rename dir %p dentry %p to dir %p dentry %p\n",
	     old_dir, old_dentry, new_dir, new_dentry);
	req = ceph_mdsc_create_request(mdsc, CEPH_MDS_OP_RENAME, USE_AUTH_MDS);
	if (IS_ERR(req))
		return PTR_ERR(req);
	req->r_dentry = dget(new_dentry);
	req->r_num_caps = 2;
	req->r_old_dentry = dget(old_dentry);
	req->r_locked_dir = new_dir;
	req->r_old_dentry_drop = CEPH_CAP_FILE_SHARED;
	req->r_old_dentry_unless = CEPH_CAP_FILE_EXCL;
	req->r_dentry_drop = CEPH_CAP_FILE_SHARED;
	req->r_dentry_unless = CEPH_CAP_FILE_EXCL;
	/* release LINK_RDCACHE on source inode (mds will lock it) */
	req->r_old_inode_drop = CEPH_CAP_LINK_SHARED;
	if (new_dentry->d_inode)
		req->r_inode_drop = drop_caps_for_unlink(new_dentry->d_inode);
	err = ceph_mdsc_do_request(mdsc, old_dir, req);
	if (!err && !req->r_reply_info.head->is_dentry) {
		/*
		 * Normally d_move() is done by fill_trace (called by
		 * do_request, above).  If there is no trace, we need
		 * to do it here.
		 */

		/* d_move screws up d_subdirs order */
		ceph_i_clear(new_dir, CEPH_I_COMPLETE);

		d_move(old_dentry, new_dentry);

		/* ensure target dentry is invalidated, despite
		   rehashing bug in vfs_rename_dir */
		ceph_invalidate_dentry_lease(new_dentry);
	}
	ceph_mdsc_put_request(req);
	return err;
}

/*
 * Ensure a dentry lease will no longer revalidate.
 */
void ceph_invalidate_dentry_lease(struct dentry *dentry)
{
	spin_lock(&dentry->d_lock);
	dentry->d_time = jiffies;
	ceph_dentry(dentry)->lease_shared_gen = 0;
	spin_unlock(&dentry->d_lock);
}

/*
 * Check if dentry lease is valid.  If not, delete the lease.  Try to
 * renew if the least is more than half up.
 */
static int dentry_lease_is_valid(struct dentry *dentry)
{
	struct ceph_dentry_info *di;
	struct ceph_mds_session *s;
	int valid = 0;
	u32 gen;
	unsigned long ttl;
	struct ceph_mds_session *session = NULL;
	struct inode *dir = NULL;
	u32 seq = 0;

	spin_lock(&dentry->d_lock);
	di = ceph_dentry(dentry);
	if (di && di->lease_session) {
		s = di->lease_session;
		spin_lock(&s->s_cap_lock);
		gen = s->s_cap_gen;
		ttl = s->s_cap_ttl;
		spin_unlock(&s->s_cap_lock);

		if (di->lease_gen == gen &&
		    time_before(jiffies, dentry->d_time) &&
		    time_before(jiffies, ttl)) {
			valid = 1;
			if (di->lease_renew_after &&
			    time_after(jiffies, di->lease_renew_after)) {
				/* we should renew */
				dir = dentry->d_parent->d_inode;
				session = ceph_get_mds_session(s);
				seq = di->lease_seq;
				di->lease_renew_after = 0;
				di->lease_renew_from = jiffies;
			}
		}
	}
	spin_unlock(&dentry->d_lock);

	if (session) {
		ceph_mdsc_lease_send_msg(session, dir, dentry,
					 CEPH_MDS_LEASE_RENEW, seq);
		ceph_put_mds_session(session);
	}
	dout("dentry_lease_is_valid - dentry %p = %d\n", dentry, valid);
	return valid;
}

/*
 * Check if directory-wide content lease/cap is valid.
 */
static int dir_lease_is_valid(struct inode *dir, struct dentry *dentry)
{
	struct ceph_inode_info *ci = ceph_inode(dir);
	struct ceph_dentry_info *di = ceph_dentry(dentry);
	int valid = 0;

	spin_lock(&dir->i_lock);
	if (ci->i_shared_gen == di->lease_shared_gen)
		valid = __ceph_caps_issued_mask(ci, CEPH_CAP_FILE_SHARED, 1);
	spin_unlock(&dir->i_lock);
	dout("dir_lease_is_valid dir %p v%u dentry %p v%u = %d\n",
	     dir, (unsigned)ci->i_shared_gen, dentry,
	     (unsigned)di->lease_shared_gen, valid);
	return valid;
}

/*
 * Check if cached dentry can be trusted.
 */
static int ceph_d_revalidate(struct dentry *dentry, struct nameidata *nd)
{
	struct inode *dir;

	if (nd->flags & LOOKUP_RCU)
		return -ECHILD;

	dir = dentry->d_parent->d_inode;

	dout("d_revalidate %p '%.*s' inode %p offset %lld\n", dentry,
	     dentry->d_name.len, dentry->d_name.name, dentry->d_inode,
	     ceph_dentry(dentry)->offset);

	/* always trust cached snapped dentries, snapdir dentry */
	if (ceph_snap(dir) != CEPH_NOSNAP) {
		dout("d_revalidate %p '%.*s' inode %p is SNAPPED\n", dentry,
		     dentry->d_name.len, dentry->d_name.name, dentry->d_inode);
		goto out_touch;
	}
	if (dentry->d_inode && ceph_snap(dentry->d_inode) == CEPH_SNAPDIR)
		goto out_touch;

	if (dentry_lease_is_valid(dentry) ||
	    dir_lease_is_valid(dir, dentry))
		goto out_touch;

	dout("d_revalidate %p invalid\n", dentry);
	d_drop(dentry);
	return 0;
out_touch:
	ceph_dentry_lru_touch(dentry);
	return 1;
}

/*
 * When a dentry is released, clear the dir I_COMPLETE if it was part
 * of the current dir gen or if this is in the snapshot namespace.
 */
static void ceph_dentry_release(struct dentry *dentry)
{
	struct ceph_dentry_info *di = ceph_dentry(dentry);
	struct inode *parent_inode = NULL;
	u64 snapid = CEPH_NOSNAP;

	if (!IS_ROOT(dentry)) {
		parent_inode = dentry->d_parent->d_inode;
		if (parent_inode)
			snapid = ceph_snap(parent_inode);
	}
	dout("dentry_release %p parent %p\n", dentry, parent_inode);
	if (parent_inode && snapid != CEPH_SNAPDIR) {
		struct ceph_inode_info *ci = ceph_inode(parent_inode);

		spin_lock(&parent_inode->i_lock);
		if (ci->i_shared_gen == di->lease_shared_gen ||
		    snapid <= CEPH_MAXSNAP) {
			dout(" clearing %p complete (d_release)\n",
			     parent_inode);
			ci->i_ceph_flags &= ~CEPH_I_COMPLETE;
			ci->i_release_count++;
		}
		spin_unlock(&parent_inode->i_lock);
	}
	if (di) {
		ceph_dentry_lru_del(dentry);
		if (di->lease_session)
			ceph_put_mds_session(di->lease_session);
		kmem_cache_free(ceph_dentry_cachep, di);
		dentry->d_fsdata = NULL;
	}
}

static int ceph_snapdir_d_revalidate(struct dentry *dentry,
					  struct nameidata *nd)
{
	/*
	 * Eventually, we'll want to revalidate snapped metadata
	 * too... probably...
	 */
	return 1;
}



/*
 * read() on a dir.  This weird interface hack only works if mounted
 * with '-o dirstat'.
 */
static ssize_t ceph_read_dir(struct file *file, char __user *buf, size_t size,
			     loff_t *ppos)
{
	struct ceph_file_info *cf = file->private_data;
	struct inode *inode = file->f_dentry->d_inode;
	struct ceph_inode_info *ci = ceph_inode(inode);
	int left;

	if (!ceph_test_mount_opt(ceph_sb_to_client(inode->i_sb), DIRSTAT))
		return -EISDIR;

	if (!cf->dir_info) {
		cf->dir_info = kmalloc(1024, GFP_NOFS);
		if (!cf->dir_info)
			return -ENOMEM;
		cf->dir_info_len =
			sprintf(cf->dir_info,
				"entries:   %20lld\n"
				" files:    %20lld\n"
				" subdirs:  %20lld\n"
				"rentries:  %20lld\n"
				" rfiles:   %20lld\n"
				" rsubdirs: %20lld\n"
				"rbytes:    %20lld\n"
				"rctime:    %10ld.%09ld\n",
				ci->i_files + ci->i_subdirs,
				ci->i_files,
				ci->i_subdirs,
				ci->i_rfiles + ci->i_rsubdirs,
				ci->i_rfiles,
				ci->i_rsubdirs,
				ci->i_rbytes,
				(long)ci->i_rctime.tv_sec,
				(long)ci->i_rctime.tv_nsec);
	}

	if (*ppos >= cf->dir_info_len)
		return 0;
	size = min_t(unsigned, size, cf->dir_info_len-*ppos);
	left = copy_to_user(buf, cf->dir_info + *ppos, size);
	if (left == size)
		return -EFAULT;
	*ppos += (size - left);
	return size - left;
}

/*
 * an fsync() on a dir will wait for any uncommitted directory
 * operations to commit.
 */
static int ceph_dir_fsync(struct file *file, int datasync)
{
	struct inode *inode = file->f_path.dentry->d_inode;
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct list_head *head = &ci->i_unsafe_dirops;
	struct ceph_mds_request *req;
	u64 last_tid;
	int ret = 0;

	dout("dir_fsync %p\n", inode);
	spin_lock(&ci->i_unsafe_lock);
	if (list_empty(head))
		goto out;

	req = list_entry(head->prev,
			 struct ceph_mds_request, r_unsafe_dir_item);
	last_tid = req->r_tid;

	do {
		ceph_mdsc_get_request(req);
		spin_unlock(&ci->i_unsafe_lock);
		dout("dir_fsync %p wait on tid %llu (until %llu)\n",
		     inode, req->r_tid, last_tid);
		if (req->r_timeout) {
			ret = wait_for_completion_timeout(
				&req->r_safe_completion, req->r_timeout);
			if (ret > 0)
				ret = 0;
			else if (ret == 0)
				ret = -EIO;  /* timed out */
		} else {
			wait_for_completion(&req->r_safe_completion);
		}
		spin_lock(&ci->i_unsafe_lock);
		ceph_mdsc_put_request(req);

		if (ret || list_empty(head))
			break;
		req = list_entry(head->next,
				 struct ceph_mds_request, r_unsafe_dir_item);
	} while (req->r_tid < last_tid);
out:
	spin_unlock(&ci->i_unsafe_lock);
	return ret;
}

/*
 * We maintain a private dentry LRU.
 *
 * FIXME: this needs to be changed to a per-mds lru to be useful.
 */
void ceph_dentry_lru_add(struct dentry *dn)
{
	struct ceph_dentry_info *di = ceph_dentry(dn);
	struct ceph_mds_client *mdsc;

	dout("dentry_lru_add %p %p '%.*s'\n", di, dn,
	     dn->d_name.len, dn->d_name.name);
	if (di) {
		mdsc = ceph_sb_to_client(dn->d_sb)->mdsc;
		spin_lock(&mdsc->dentry_lru_lock);
		list_add_tail(&di->lru, &mdsc->dentry_lru);
		mdsc->num_dentry++;
		spin_unlock(&mdsc->dentry_lru_lock);
	}
}

void ceph_dentry_lru_touch(struct dentry *dn)
{
	struct ceph_dentry_info *di = ceph_dentry(dn);
	struct ceph_mds_client *mdsc;

	dout("dentry_lru_touch %p %p '%.*s' (offset %lld)\n", di, dn,
	     dn->d_name.len, dn->d_name.name, di->offset);
	if (di) {
		mdsc = ceph_sb_to_client(dn->d_sb)->mdsc;
		spin_lock(&mdsc->dentry_lru_lock);
		list_move_tail(&di->lru, &mdsc->dentry_lru);
		spin_unlock(&mdsc->dentry_lru_lock);
	}
}

void ceph_dentry_lru_del(struct dentry *dn)
{
	struct ceph_dentry_info *di = ceph_dentry(dn);
	struct ceph_mds_client *mdsc;

	dout("dentry_lru_del %p %p '%.*s'\n", di, dn,
	     dn->d_name.len, dn->d_name.name);
	if (di) {
		mdsc = ceph_sb_to_client(dn->d_sb)->mdsc;
		spin_lock(&mdsc->dentry_lru_lock);
		list_del_init(&di->lru);
		mdsc->num_dentry--;
		spin_unlock(&mdsc->dentry_lru_lock);
	}
}

/*
 * Return name hash for a given dentry.  This is dependent on
 * the parent directory's hash function.
 */
unsigned ceph_dentry_hash(struct dentry *dn)
{
	struct inode *dir = dn->d_parent->d_inode;
	struct ceph_inode_info *dci = ceph_inode(dir);

	switch (dci->i_dir_layout.dl_dir_hash) {
	case 0:	/* for backward compat */
	case CEPH_STR_HASH_LINUX:
		return dn->d_name.hash;

	default:
		return ceph_str_hash(dci->i_dir_layout.dl_dir_hash,
				     dn->d_name.name, dn->d_name.len);
	}
}

const struct file_operations ceph_dir_fops = {
	.read = ceph_read_dir,
	.readdir = ceph_readdir,
	.llseek = ceph_dir_llseek,
	.open = ceph_open,
	.release = ceph_release,
	.unlocked_ioctl = ceph_ioctl,
	.fsync = ceph_dir_fsync,
};

const struct inode_operations ceph_dir_iops = {
	.lookup = ceph_lookup,
	.permission = ceph_permission,
	.getattr = ceph_getattr,
	.setattr = ceph_setattr,
	.setxattr = ceph_setxattr,
	.getxattr = ceph_getxattr,
	.listxattr = ceph_listxattr,
	.removexattr = ceph_removexattr,
	.mknod = ceph_mknod,
	.symlink = ceph_symlink,
	.mkdir = ceph_mkdir,
	.link = ceph_link,
	.unlink = ceph_unlink,
	.rmdir = ceph_unlink,
	.rename = ceph_rename,
	.create = ceph_create,
};

const struct dentry_operations ceph_dentry_ops = {
	.d_revalidate = ceph_d_revalidate,
	.d_release = ceph_dentry_release,
};

const struct dentry_operations ceph_snapdir_dentry_ops = {
	.d_revalidate = ceph_snapdir_d_revalidate,
	.d_release = ceph_dentry_release,
};

const struct dentry_operations ceph_snap_dentry_ops = {
	.d_release = ceph_dentry_release,
};
