#include <linux/ceph/ceph_debug.h>

#include <linux/spinlock.h>
#include <linux/fs_struct.h>
#include <linux/namei.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/xattr.h>

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

const struct dentry_operations ceph_dentry_ops;

/*
 * Initialize ceph dentry state.
 */
static int ceph_d_init(struct dentry *dentry)
{
	struct ceph_dentry_info *di;

	di = kmem_cache_zalloc(ceph_dentry_cachep, GFP_KERNEL);
	if (!di)
		return -ENOMEM;          /* oh well */

	di->dentry = dentry;
	di->lease_session = NULL;
	di->time = jiffies;
	dentry->d_fsdata = di;
	ceph_dentry_lru_add(dentry);
	return 0;
}

/*
 * for f_pos for readdir:
 * - hash order:
 *	(0xff << 52) | ((24 bits hash) << 28) |
 *	(the nth entry has hash collision);
 * - frag+name order;
 *	((frag value) << 28) | (the nth entry in frag);
 */
#define OFFSET_BITS	28
#define OFFSET_MASK	((1 << OFFSET_BITS) - 1)
#define HASH_ORDER	(0xffull << (OFFSET_BITS + 24))
loff_t ceph_make_fpos(unsigned high, unsigned off, bool hash_order)
{
	loff_t fpos = ((loff_t)high << 28) | (loff_t)off;
	if (hash_order)
		fpos |= HASH_ORDER;
	return fpos;
}

static bool is_hash_order(loff_t p)
{
	return (p & HASH_ORDER) == HASH_ORDER;
}

static unsigned fpos_frag(loff_t p)
{
	return p >> OFFSET_BITS;
}

static unsigned fpos_hash(loff_t p)
{
	return ceph_frag_value(fpos_frag(p));
}

static unsigned fpos_off(loff_t p)
{
	return p & OFFSET_MASK;
}

static int fpos_cmp(loff_t l, loff_t r)
{
	int v = ceph_frag_compare(fpos_frag(l), fpos_frag(r));
	if (v)
		return v;
	return (int)(fpos_off(l) - fpos_off(r));
}

/*
 * make note of the last dentry we read, so we can
 * continue at the same lexicographical point,
 * regardless of what dir changes take place on the
 * server.
 */
static int note_last_dentry(struct ceph_file_info *fi, const char *name,
		            int len, unsigned next_offset)
{
	char *buf = kmalloc(len+1, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	kfree(fi->last_name);
	fi->last_name = buf;
	memcpy(fi->last_name, name, len);
	fi->last_name[len] = 0;
	fi->next_offset = next_offset;
	dout("note_last_dentry '%s'\n", fi->last_name);
	return 0;
}


static struct dentry *
__dcache_find_get_entry(struct dentry *parent, u64 idx,
			struct ceph_readdir_cache_control *cache_ctl)
{
	struct inode *dir = d_inode(parent);
	struct dentry *dentry;
	unsigned idx_mask = (PAGE_SIZE / sizeof(struct dentry *)) - 1;
	loff_t ptr_pos = idx * sizeof(struct dentry *);
	pgoff_t ptr_pgoff = ptr_pos >> PAGE_SHIFT;

	if (ptr_pos >= i_size_read(dir))
		return NULL;

	if (!cache_ctl->page || ptr_pgoff != page_index(cache_ctl->page)) {
		ceph_readdir_cache_release(cache_ctl);
		cache_ctl->page = find_lock_page(&dir->i_data, ptr_pgoff);
		if (!cache_ctl->page) {
			dout(" page %lu not found\n", ptr_pgoff);
			return ERR_PTR(-EAGAIN);
		}
		/* reading/filling the cache are serialized by
		   i_mutex, no need to use page lock */
		unlock_page(cache_ctl->page);
		cache_ctl->dentries = kmap(cache_ctl->page);
	}

	cache_ctl->index = idx & idx_mask;

	rcu_read_lock();
	spin_lock(&parent->d_lock);
	/* check i_size again here, because empty directory can be
	 * marked as complete while not holding the i_mutex. */
	if (ceph_dir_is_complete_ordered(dir) && ptr_pos < i_size_read(dir))
		dentry = cache_ctl->dentries[cache_ctl->index];
	else
		dentry = NULL;
	spin_unlock(&parent->d_lock);
	if (dentry && !lockref_get_not_dead(&dentry->d_lockref))
		dentry = NULL;
	rcu_read_unlock();
	return dentry ? : ERR_PTR(-EAGAIN);
}

/*
 * When possible, we try to satisfy a readdir by peeking at the
 * dcache.  We make this work by carefully ordering dentries on
 * d_child when we initially get results back from the MDS, and
 * falling back to a "normal" sync readdir if any dentries in the dir
 * are dropped.
 *
 * Complete dir indicates that we have all dentries in the dir.  It is
 * defined IFF we hold CEPH_CAP_FILE_SHARED (which will be revoked by
 * the MDS if/when the directory is modified).
 */
static int __dcache_readdir(struct file *file,  struct dir_context *ctx,
			    u32 shared_gen)
{
	struct ceph_file_info *fi = file->private_data;
	struct dentry *parent = file->f_path.dentry;
	struct inode *dir = d_inode(parent);
	struct dentry *dentry, *last = NULL;
	struct ceph_dentry_info *di;
	struct ceph_readdir_cache_control cache_ctl = {};
	u64 idx = 0;
	int err = 0;

	dout("__dcache_readdir %p v%u at %llx\n", dir, shared_gen, ctx->pos);

	/* search start position */
	if (ctx->pos > 2) {
		u64 count = div_u64(i_size_read(dir), sizeof(struct dentry *));
		while (count > 0) {
			u64 step = count >> 1;
			dentry = __dcache_find_get_entry(parent, idx + step,
							 &cache_ctl);
			if (!dentry) {
				/* use linar search */
				idx = 0;
				break;
			}
			if (IS_ERR(dentry)) {
				err = PTR_ERR(dentry);
				goto out;
			}
			di = ceph_dentry(dentry);
			spin_lock(&dentry->d_lock);
			if (fpos_cmp(di->offset, ctx->pos) < 0) {
				idx += step + 1;
				count -= step + 1;
			} else {
				count = step;
			}
			spin_unlock(&dentry->d_lock);
			dput(dentry);
		}

		dout("__dcache_readdir %p cache idx %llu\n", dir, idx);
	}


	for (;;) {
		bool emit_dentry = false;
		dentry = __dcache_find_get_entry(parent, idx++, &cache_ctl);
		if (!dentry) {
			fi->flags |= CEPH_F_ATEND;
			err = 0;
			break;
		}
		if (IS_ERR(dentry)) {
			err = PTR_ERR(dentry);
			goto out;
		}

		di = ceph_dentry(dentry);
		spin_lock(&dentry->d_lock);
		if (di->lease_shared_gen == shared_gen &&
		    d_really_is_positive(dentry) &&
		    fpos_cmp(ctx->pos, di->offset) <= 0) {
			emit_dentry = true;
		}
		spin_unlock(&dentry->d_lock);

		if (emit_dentry) {
			dout(" %llx dentry %p %pd %p\n", di->offset,
			     dentry, dentry, d_inode(dentry));
			ctx->pos = di->offset;
			if (!dir_emit(ctx, dentry->d_name.name,
				      dentry->d_name.len,
				      ceph_translate_ino(dentry->d_sb,
							 d_inode(dentry)->i_ino),
				      d_inode(dentry)->i_mode >> 12)) {
				dput(dentry);
				err = 0;
				break;
			}
			ctx->pos++;

			if (last)
				dput(last);
			last = dentry;
		} else {
			dput(dentry);
		}
	}
out:
	ceph_readdir_cache_release(&cache_ctl);
	if (last) {
		int ret;
		di = ceph_dentry(last);
		ret = note_last_dentry(fi, last->d_name.name, last->d_name.len,
				       fpos_off(di->offset) + 1);
		if (ret < 0)
			err = ret;
		dput(last);
		/* last_name no longer match cache index */
		if (fi->readdir_cache_idx >= 0) {
			fi->readdir_cache_idx = -1;
			fi->dir_release_count = 0;
		}
	}
	return err;
}

static bool need_send_readdir(struct ceph_file_info *fi, loff_t pos)
{
	if (!fi->last_readdir)
		return true;
	if (is_hash_order(pos))
		return !ceph_frag_contains_value(fi->frag, fpos_hash(pos));
	else
		return fi->frag != fpos_frag(pos);
}

static int ceph_readdir(struct file *file, struct dir_context *ctx)
{
	struct ceph_file_info *fi = file->private_data;
	struct inode *inode = file_inode(file);
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_fs_client *fsc = ceph_inode_to_client(inode);
	struct ceph_mds_client *mdsc = fsc->mdsc;
	int i;
	int err;
	unsigned frag = -1;
	struct ceph_mds_reply_info_parsed *rinfo;

	dout("readdir %p file %p pos %llx\n", inode, file, ctx->pos);
	if (fi->flags & CEPH_F_ATEND)
		return 0;

	/* always start with . and .. */
	if (ctx->pos == 0) {
		dout("readdir off 0 -> '.'\n");
		if (!dir_emit(ctx, ".", 1, 
			    ceph_translate_ino(inode->i_sb, inode->i_ino),
			    inode->i_mode >> 12))
			return 0;
		ctx->pos = 1;
	}
	if (ctx->pos == 1) {
		ino_t ino = parent_ino(file->f_path.dentry);
		dout("readdir off 1 -> '..'\n");
		if (!dir_emit(ctx, "..", 2,
			    ceph_translate_ino(inode->i_sb, ino),
			    inode->i_mode >> 12))
			return 0;
		ctx->pos = 2;
	}

	/* can we use the dcache? */
	spin_lock(&ci->i_ceph_lock);
	if (ceph_test_mount_opt(fsc, DCACHE) &&
	    !ceph_test_mount_opt(fsc, NOASYNCREADDIR) &&
	    ceph_snap(inode) != CEPH_SNAPDIR &&
	    __ceph_dir_is_complete_ordered(ci) &&
	    __ceph_caps_issued_mask(ci, CEPH_CAP_FILE_SHARED, 1)) {
		u32 shared_gen = ci->i_shared_gen;
		spin_unlock(&ci->i_ceph_lock);
		err = __dcache_readdir(file, ctx, shared_gen);
		if (err != -EAGAIN)
			return err;
	} else {
		spin_unlock(&ci->i_ceph_lock);
	}

	/* proceed with a normal readdir */
more:
	/* do we have the correct frag content buffered? */
	if (need_send_readdir(fi, ctx->pos)) {
		struct ceph_mds_request *req;
		int op = ceph_snap(inode) == CEPH_SNAPDIR ?
			CEPH_MDS_OP_LSSNAP : CEPH_MDS_OP_READDIR;

		/* discard old result, if any */
		if (fi->last_readdir) {
			ceph_mdsc_put_request(fi->last_readdir);
			fi->last_readdir = NULL;
		}

		if (is_hash_order(ctx->pos)) {
			/* fragtree isn't always accurate. choose frag
			 * based on previous reply when possible. */
			if (frag == (unsigned)-1)
				frag = ceph_choose_frag(ci, fpos_hash(ctx->pos),
							NULL, NULL);
		} else {
			frag = fpos_frag(ctx->pos);
		}

		dout("readdir fetching %llx.%llx frag %x offset '%s'\n",
		     ceph_vinop(inode), frag, fi->last_name);
		req = ceph_mdsc_create_request(mdsc, op, USE_AUTH_MDS);
		if (IS_ERR(req))
			return PTR_ERR(req);
		err = ceph_alloc_readdir_reply_buffer(req, inode);
		if (err) {
			ceph_mdsc_put_request(req);
			return err;
		}
		/* hints to request -> mds selection code */
		req->r_direct_mode = USE_AUTH_MDS;
		req->r_direct_hash = ceph_frag_value(frag);
		__set_bit(CEPH_MDS_R_DIRECT_IS_HASH, &req->r_req_flags);
		if (fi->last_name) {
			req->r_path2 = kstrdup(fi->last_name, GFP_KERNEL);
			if (!req->r_path2) {
				ceph_mdsc_put_request(req);
				return -ENOMEM;
			}
		} else if (is_hash_order(ctx->pos)) {
			req->r_args.readdir.offset_hash =
				cpu_to_le32(fpos_hash(ctx->pos));
		}

		req->r_dir_release_cnt = fi->dir_release_count;
		req->r_dir_ordered_cnt = fi->dir_ordered_count;
		req->r_readdir_cache_idx = fi->readdir_cache_idx;
		req->r_readdir_offset = fi->next_offset;
		req->r_args.readdir.frag = cpu_to_le32(frag);
		req->r_args.readdir.flags =
				cpu_to_le16(CEPH_READDIR_REPLY_BITFLAGS);

		req->r_inode = inode;
		ihold(inode);
		req->r_dentry = dget(file->f_path.dentry);
		err = ceph_mdsc_do_request(mdsc, NULL, req);
		if (err < 0) {
			ceph_mdsc_put_request(req);
			return err;
		}
		dout("readdir got and parsed readdir result=%d on "
		     "frag %x, end=%d, complete=%d, hash_order=%d\n",
		     err, frag,
		     (int)req->r_reply_info.dir_end,
		     (int)req->r_reply_info.dir_complete,
		     (int)req->r_reply_info.hash_order);

		rinfo = &req->r_reply_info;
		if (le32_to_cpu(rinfo->dir_dir->frag) != frag) {
			frag = le32_to_cpu(rinfo->dir_dir->frag);
			if (!rinfo->hash_order) {
				fi->next_offset = req->r_readdir_offset;
				/* adjust ctx->pos to beginning of frag */
				ctx->pos = ceph_make_fpos(frag,
							  fi->next_offset,
							  false);
			}
		}

		fi->frag = frag;
		fi->last_readdir = req;

		if (test_bit(CEPH_MDS_R_DID_PREPOPULATE, &req->r_req_flags)) {
			fi->readdir_cache_idx = req->r_readdir_cache_idx;
			if (fi->readdir_cache_idx < 0) {
				/* preclude from marking dir ordered */
				fi->dir_ordered_count = 0;
			} else if (ceph_frag_is_leftmost(frag) &&
				   fi->next_offset == 2) {
				/* note dir version at start of readdir so
				 * we can tell if any dentries get dropped */
				fi->dir_release_count = req->r_dir_release_cnt;
				fi->dir_ordered_count = req->r_dir_ordered_cnt;
			}
		} else {
			dout("readdir !did_prepopulate");
			/* disable readdir cache */
			fi->readdir_cache_idx = -1;
			/* preclude from marking dir complete */
			fi->dir_release_count = 0;
		}

		/* note next offset and last dentry name */
		if (rinfo->dir_nr > 0) {
			struct ceph_mds_reply_dir_entry *rde =
					rinfo->dir_entries + (rinfo->dir_nr-1);
			unsigned next_offset = req->r_reply_info.dir_end ?
					2 : (fpos_off(rde->offset) + 1);
			err = note_last_dentry(fi, rde->name, rde->name_len,
					       next_offset);
			if (err)
				return err;
		} else if (req->r_reply_info.dir_end) {
			fi->next_offset = 2;
			/* keep last name */
		}
	}

	rinfo = &fi->last_readdir->r_reply_info;
	dout("readdir frag %x num %d pos %llx chunk first %llx\n",
	     fi->frag, rinfo->dir_nr, ctx->pos,
	     rinfo->dir_nr ? rinfo->dir_entries[0].offset : 0LL);

	i = 0;
	/* search start position */
	if (rinfo->dir_nr > 0) {
		int step, nr = rinfo->dir_nr;
		while (nr > 0) {
			step = nr >> 1;
			if (rinfo->dir_entries[i + step].offset < ctx->pos) {
				i +=  step + 1;
				nr -= step + 1;
			} else {
				nr = step;
			}
		}
	}
	for (; i < rinfo->dir_nr; i++) {
		struct ceph_mds_reply_dir_entry *rde = rinfo->dir_entries + i;
		struct ceph_vino vino;
		ino_t ino;
		u32 ftype;

		BUG_ON(rde->offset < ctx->pos);

		ctx->pos = rde->offset;
		dout("readdir (%d/%d) -> %llx '%.*s' %p\n",
		     i, rinfo->dir_nr, ctx->pos,
		     rde->name_len, rde->name, &rde->inode.in);

		BUG_ON(!rde->inode.in);
		ftype = le32_to_cpu(rde->inode.in->mode) >> 12;
		vino.ino = le64_to_cpu(rde->inode.in->ino);
		vino.snap = le64_to_cpu(rde->inode.in->snapid);
		ino = ceph_vino_to_ino(vino);

		if (!dir_emit(ctx, rde->name, rde->name_len,
			      ceph_translate_ino(inode->i_sb, ino), ftype)) {
			dout("filldir stopping us...\n");
			return 0;
		}
		ctx->pos++;
	}

	ceph_mdsc_put_request(fi->last_readdir);
	fi->last_readdir = NULL;

	if (fi->next_offset > 2) {
		frag = fi->frag;
		goto more;
	}

	/* more frags? */
	if (!ceph_frag_is_rightmost(fi->frag)) {
		frag = ceph_frag_next(fi->frag);
		if (is_hash_order(ctx->pos)) {
			loff_t new_pos = ceph_make_fpos(ceph_frag_value(frag),
							fi->next_offset, true);
			if (new_pos > ctx->pos)
				ctx->pos = new_pos;
			/* keep last_name */
		} else {
			ctx->pos = ceph_make_fpos(frag, fi->next_offset, false);
			kfree(fi->last_name);
			fi->last_name = NULL;
		}
		dout("readdir next frag is %x\n", frag);
		goto more;
	}
	fi->flags |= CEPH_F_ATEND;

	/*
	 * if dir_release_count still matches the dir, no dentries
	 * were released during the whole readdir, and we should have
	 * the complete dir contents in our cache.
	 */
	if (atomic64_read(&ci->i_release_count) == fi->dir_release_count) {
		spin_lock(&ci->i_ceph_lock);
		if (fi->dir_ordered_count == atomic64_read(&ci->i_ordered_count)) {
			dout(" marking %p complete and ordered\n", inode);
			/* use i_size to track number of entries in
			 * readdir cache */
			BUG_ON(fi->readdir_cache_idx < 0);
			i_size_write(inode, fi->readdir_cache_idx *
				     sizeof(struct dentry*));
		} else {
			dout(" marking %p complete\n", inode);
		}
		__ceph_dir_set_complete(ci, fi->dir_release_count,
					fi->dir_ordered_count);
		spin_unlock(&ci->i_ceph_lock);
	}

	dout("readdir %p file %p done.\n", inode, file);
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
	fi->dir_release_count = 0;
	fi->readdir_cache_idx = -1;
	fi->next_offset = 2;  /* compensate for . and .. */
	fi->flags &= ~CEPH_F_ATEND;
}

/*
 * discard buffered readdir content on seekdir(0), or seek to new frag,
 * or seek prior to current chunk
 */
static bool need_reset_readdir(struct ceph_file_info *fi, loff_t new_pos)
{
	struct ceph_mds_reply_info_parsed *rinfo;
	loff_t chunk_offset;
	if (new_pos == 0)
		return true;
	if (is_hash_order(new_pos)) {
		/* no need to reset last_name for a forward seek when
		 * dentries are sotred in hash order */
	} else if (fi->frag != fpos_frag(new_pos)) {
		return true;
	}
	rinfo = fi->last_readdir ? &fi->last_readdir->r_reply_info : NULL;
	if (!rinfo || !rinfo->dir_nr)
		return true;
	chunk_offset = rinfo->dir_entries[0].offset;
	return new_pos < chunk_offset ||
	       is_hash_order(new_pos) != is_hash_order(chunk_offset);
}

static loff_t ceph_dir_llseek(struct file *file, loff_t offset, int whence)
{
	struct ceph_file_info *fi = file->private_data;
	struct inode *inode = file->f_mapping->host;
	loff_t retval;

	inode_lock(inode);
	retval = -EINVAL;
	switch (whence) {
	case SEEK_CUR:
		offset += file->f_pos;
	case SEEK_SET:
		break;
	case SEEK_END:
		retval = -EOPNOTSUPP;
	default:
		goto out;
	}

	if (offset >= 0) {
		if (need_reset_readdir(fi, offset)) {
			dout("dir_llseek dropping %p content\n", file);
			reset_readdir(fi);
		} else if (is_hash_order(offset) && offset > file->f_pos) {
			/* for hash offset, we don't know if a forward seek
			 * is within same frag */
			fi->dir_release_count = 0;
			fi->readdir_cache_idx = -1;
		}

		if (offset != file->f_pos) {
			file->f_pos = offset;
			file->f_version = 0;
			fi->flags &= ~CEPH_F_ATEND;
		}
		retval = offset;
	}
out:
	inode_unlock(inode);
	return retval;
}

/*
 * Handle lookups for the hidden .snap directory.
 */
int ceph_handle_snapdir(struct ceph_mds_request *req,
			struct dentry *dentry, int err)
{
	struct ceph_fs_client *fsc = ceph_sb_to_client(dentry->d_sb);
	struct inode *parent = d_inode(dentry->d_parent); /* we hold i_mutex */

	/* .snap dir? */
	if (err == -ENOENT &&
	    ceph_snap(parent) == CEPH_NOSNAP &&
	    strcmp(dentry->d_name.name,
		   fsc->mount_options->snapdir_name) == 0) {
		struct inode *inode = ceph_get_snapdir(parent);
		dout("ENOENT on snapdir %p '%pd', linking to snapdir %p\n",
		     dentry, dentry, inode);
		BUG_ON(!d_unhashed(dentry));
		d_add(dentry, inode);
		err = 0;
	}
	return err;
}

/*
 * Figure out final result of a lookup/open request.
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
	if (err == -ENOENT) {
		/* no trace? */
		err = 0;
		if (!req->r_reply_info.head->is_dentry) {
			dout("ENOENT and no trace, dentry %p inode %p\n",
			     dentry, d_inode(dentry));
			if (d_really_is_positive(dentry)) {
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

static bool is_root_ceph_dentry(struct inode *inode, struct dentry *dentry)
{
	return ceph_ino(inode) == CEPH_INO_ROOT &&
		strncmp(dentry->d_name.name, ".ceph", 5) == 0;
}

/*
 * Look up a single dir entry.  If there is a lookup intent, inform
 * the MDS so that it gets our 'caps wanted' value in a single op.
 */
static struct dentry *ceph_lookup(struct inode *dir, struct dentry *dentry,
				  unsigned int flags)
{
	struct ceph_fs_client *fsc = ceph_sb_to_client(dir->i_sb);
	struct ceph_mds_client *mdsc = fsc->mdsc;
	struct ceph_mds_request *req;
	int op;
	int mask;
	int err;

	dout("lookup %p dentry %p '%pd'\n",
	     dir, dentry, dentry);

	if (dentry->d_name.len > NAME_MAX)
		return ERR_PTR(-ENAMETOOLONG);

	/* can we conclude ENOENT locally? */
	if (d_really_is_negative(dentry)) {
		struct ceph_inode_info *ci = ceph_inode(dir);
		struct ceph_dentry_info *di = ceph_dentry(dentry);

		spin_lock(&ci->i_ceph_lock);
		dout(" dir %p flags are %d\n", dir, ci->i_ceph_flags);
		if (strncmp(dentry->d_name.name,
			    fsc->mount_options->snapdir_name,
			    dentry->d_name.len) &&
		    !is_root_ceph_dentry(dir, dentry) &&
		    ceph_test_mount_opt(fsc, DCACHE) &&
		    __ceph_dir_is_complete(ci) &&
		    (__ceph_caps_issued_mask(ci, CEPH_CAP_FILE_SHARED, 1))) {
			spin_unlock(&ci->i_ceph_lock);
			dout(" dir %p complete, -ENOENT\n", dir);
			d_add(dentry, NULL);
			di->lease_shared_gen = ci->i_shared_gen;
			return NULL;
		}
		spin_unlock(&ci->i_ceph_lock);
	}

	op = ceph_snap(dir) == CEPH_SNAPDIR ?
		CEPH_MDS_OP_LOOKUPSNAP : CEPH_MDS_OP_LOOKUP;
	req = ceph_mdsc_create_request(mdsc, op, USE_ANY_MDS);
	if (IS_ERR(req))
		return ERR_CAST(req);
	req->r_dentry = dget(dentry);
	req->r_num_caps = 2;

	mask = CEPH_STAT_CAP_INODE | CEPH_CAP_AUTH_SHARED;
	if (ceph_security_xattr_wanted(dir))
		mask |= CEPH_CAP_XATTR_SHARED;
	req->r_args.getattr.mask = cpu_to_le32(mask);

	req->r_parent = dir;
	set_bit(CEPH_MDS_R_PARENT_LOCKED, &req->r_req_flags);
	err = ceph_mdsc_do_request(mdsc, NULL, req);
	err = ceph_handle_snapdir(req, dentry, err);
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
	struct dentry *result = ceph_lookup(dir, dentry, 0);

	if (result && !IS_ERR(result)) {
		/*
		 * We created the item, then did a lookup, and found
		 * it was already linked to another inode we already
		 * had in our cache (and thus got spliced). To not
		 * confuse VFS (especially when inode is a directory),
		 * we don't link our dentry to that inode, return an
		 * error instead.
		 *
		 * This event should be rare and it happens only when
		 * we talk to old MDS. Recent MDS does not send traceless
		 * reply for request that creates new inode.
		 */
		d_drop(result);
		return -ESTALE;
	}
	return PTR_ERR(result);
}

static int ceph_mknod(struct inode *dir, struct dentry *dentry,
		      umode_t mode, dev_t rdev)
{
	struct ceph_fs_client *fsc = ceph_sb_to_client(dir->i_sb);
	struct ceph_mds_client *mdsc = fsc->mdsc;
	struct ceph_mds_request *req;
	struct ceph_acls_info acls = {};
	int err;

	if (ceph_snap(dir) != CEPH_NOSNAP)
		return -EROFS;

	err = ceph_pre_init_acls(dir, &mode, &acls);
	if (err < 0)
		return err;

	dout("mknod in dir %p dentry %p mode 0%ho rdev %d\n",
	     dir, dentry, mode, rdev);
	req = ceph_mdsc_create_request(mdsc, CEPH_MDS_OP_MKNOD, USE_AUTH_MDS);
	if (IS_ERR(req)) {
		err = PTR_ERR(req);
		goto out;
	}
	req->r_dentry = dget(dentry);
	req->r_num_caps = 2;
	req->r_parent = dir;
	set_bit(CEPH_MDS_R_PARENT_LOCKED, &req->r_req_flags);
	req->r_args.mknod.mode = cpu_to_le32(mode);
	req->r_args.mknod.rdev = cpu_to_le32(rdev);
	req->r_dentry_drop = CEPH_CAP_FILE_SHARED;
	req->r_dentry_unless = CEPH_CAP_FILE_EXCL;
	if (acls.pagelist) {
		req->r_pagelist = acls.pagelist;
		acls.pagelist = NULL;
	}
	err = ceph_mdsc_do_request(mdsc, dir, req);
	if (!err && !req->r_reply_info.head->is_dentry)
		err = ceph_handle_notrace_create(dir, dentry);
	ceph_mdsc_put_request(req);
out:
	if (!err)
		ceph_init_inode_acls(d_inode(dentry), &acls);
	else
		d_drop(dentry);
	ceph_release_acls_info(&acls);
	return err;
}

static int ceph_create(struct inode *dir, struct dentry *dentry, umode_t mode,
		       bool excl)
{
	return ceph_mknod(dir, dentry, mode, 0);
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
		err = PTR_ERR(req);
		goto out;
	}
	req->r_path2 = kstrdup(dest, GFP_KERNEL);
	if (!req->r_path2) {
		err = -ENOMEM;
		ceph_mdsc_put_request(req);
		goto out;
	}
	req->r_parent = dir;
	set_bit(CEPH_MDS_R_PARENT_LOCKED, &req->r_req_flags);
	req->r_dentry = dget(dentry);
	req->r_num_caps = 2;
	req->r_dentry_drop = CEPH_CAP_FILE_SHARED;
	req->r_dentry_unless = CEPH_CAP_FILE_EXCL;
	err = ceph_mdsc_do_request(mdsc, dir, req);
	if (!err && !req->r_reply_info.head->is_dentry)
		err = ceph_handle_notrace_create(dir, dentry);
	ceph_mdsc_put_request(req);
out:
	if (err)
		d_drop(dentry);
	return err;
}

static int ceph_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	struct ceph_fs_client *fsc = ceph_sb_to_client(dir->i_sb);
	struct ceph_mds_client *mdsc = fsc->mdsc;
	struct ceph_mds_request *req;
	struct ceph_acls_info acls = {};
	int err = -EROFS;
	int op;

	if (ceph_snap(dir) == CEPH_SNAPDIR) {
		/* mkdir .snap/foo is a MKSNAP */
		op = CEPH_MDS_OP_MKSNAP;
		dout("mksnap dir %p snap '%pd' dn %p\n", dir,
		     dentry, dentry);
	} else if (ceph_snap(dir) == CEPH_NOSNAP) {
		dout("mkdir dir %p dn %p mode 0%ho\n", dir, dentry, mode);
		op = CEPH_MDS_OP_MKDIR;
	} else {
		goto out;
	}

	mode |= S_IFDIR;
	err = ceph_pre_init_acls(dir, &mode, &acls);
	if (err < 0)
		goto out;

	req = ceph_mdsc_create_request(mdsc, op, USE_AUTH_MDS);
	if (IS_ERR(req)) {
		err = PTR_ERR(req);
		goto out;
	}

	req->r_dentry = dget(dentry);
	req->r_num_caps = 2;
	req->r_parent = dir;
	set_bit(CEPH_MDS_R_PARENT_LOCKED, &req->r_req_flags);
	req->r_args.mkdir.mode = cpu_to_le32(mode);
	req->r_dentry_drop = CEPH_CAP_FILE_SHARED;
	req->r_dentry_unless = CEPH_CAP_FILE_EXCL;
	if (acls.pagelist) {
		req->r_pagelist = acls.pagelist;
		acls.pagelist = NULL;
	}
	err = ceph_mdsc_do_request(mdsc, dir, req);
	if (!err &&
	    !req->r_reply_info.head->is_target &&
	    !req->r_reply_info.head->is_dentry)
		err = ceph_handle_notrace_create(dir, dentry);
	ceph_mdsc_put_request(req);
out:
	if (!err)
		ceph_init_inode_acls(d_inode(dentry), &acls);
	else
		d_drop(dentry);
	ceph_release_acls_info(&acls);
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
	req->r_old_dentry = dget(old_dentry);
	req->r_parent = dir;
	set_bit(CEPH_MDS_R_PARENT_LOCKED, &req->r_req_flags);
	req->r_dentry_drop = CEPH_CAP_FILE_SHARED;
	req->r_dentry_unless = CEPH_CAP_FILE_EXCL;
	/* release LINK_SHARED on source inode (mds will lock it) */
	req->r_old_inode_drop = CEPH_CAP_LINK_SHARED;
	err = ceph_mdsc_do_request(mdsc, dir, req);
	if (err) {
		d_drop(dentry);
	} else if (!req->r_reply_info.head->is_dentry) {
		ihold(d_inode(old_dentry));
		d_instantiate(dentry, d_inode(old_dentry));
	}
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

	spin_lock(&ci->i_ceph_lock);
	if (inode->i_nlink == 1) {
		drop |= ~(__ceph_caps_wanted(ci) | CEPH_CAP_PIN);
		ci->i_ceph_flags |= CEPH_I_NODELAY;
	}
	spin_unlock(&ci->i_ceph_lock);
	return drop;
}

/*
 * rmdir and unlink are differ only by the metadata op code
 */
static int ceph_unlink(struct inode *dir, struct dentry *dentry)
{
	struct ceph_fs_client *fsc = ceph_sb_to_client(dir->i_sb);
	struct ceph_mds_client *mdsc = fsc->mdsc;
	struct inode *inode = d_inode(dentry);
	struct ceph_mds_request *req;
	int err = -EROFS;
	int op;

	if (ceph_snap(dir) == CEPH_SNAPDIR) {
		/* rmdir .snap/foo is RMSNAP */
		dout("rmsnap dir %p '%pd' dn %p\n", dir, dentry, dentry);
		op = CEPH_MDS_OP_RMSNAP;
	} else if (ceph_snap(dir) == CEPH_NOSNAP) {
		dout("unlink/rmdir dir %p dn %p inode %p\n",
		     dir, dentry, inode);
		op = d_is_dir(dentry) ?
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
	req->r_parent = dir;
	set_bit(CEPH_MDS_R_PARENT_LOCKED, &req->r_req_flags);
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
		       struct inode *new_dir, struct dentry *new_dentry,
		       unsigned int flags)
{
	struct ceph_fs_client *fsc = ceph_sb_to_client(old_dir->i_sb);
	struct ceph_mds_client *mdsc = fsc->mdsc;
	struct ceph_mds_request *req;
	int op = CEPH_MDS_OP_RENAME;
	int err;

	if (flags)
		return -EINVAL;

	if (ceph_snap(old_dir) != ceph_snap(new_dir))
		return -EXDEV;
	if (ceph_snap(old_dir) != CEPH_NOSNAP) {
		if (old_dir == new_dir && ceph_snap(old_dir) == CEPH_SNAPDIR)
			op = CEPH_MDS_OP_RENAMESNAP;
		else
			return -EROFS;
	}
	dout("rename dir %p dentry %p to dir %p dentry %p\n",
	     old_dir, old_dentry, new_dir, new_dentry);
	req = ceph_mdsc_create_request(mdsc, op, USE_AUTH_MDS);
	if (IS_ERR(req))
		return PTR_ERR(req);
	ihold(old_dir);
	req->r_dentry = dget(new_dentry);
	req->r_num_caps = 2;
	req->r_old_dentry = dget(old_dentry);
	req->r_old_dentry_dir = old_dir;
	req->r_parent = new_dir;
	set_bit(CEPH_MDS_R_PARENT_LOCKED, &req->r_req_flags);
	req->r_old_dentry_drop = CEPH_CAP_FILE_SHARED;
	req->r_old_dentry_unless = CEPH_CAP_FILE_EXCL;
	req->r_dentry_drop = CEPH_CAP_FILE_SHARED;
	req->r_dentry_unless = CEPH_CAP_FILE_EXCL;
	/* release LINK_RDCACHE on source inode (mds will lock it) */
	req->r_old_inode_drop = CEPH_CAP_LINK_SHARED;
	if (d_really_is_positive(new_dentry))
		req->r_inode_drop = drop_caps_for_unlink(d_inode(new_dentry));
	err = ceph_mdsc_do_request(mdsc, old_dir, req);
	if (!err && !req->r_reply_info.head->is_dentry) {
		/*
		 * Normally d_move() is done by fill_trace (called by
		 * do_request, above).  If there is no trace, we need
		 * to do it here.
		 */

		/* d_move screws up sibling dentries' offsets */
		ceph_dir_clear_complete(old_dir);
		ceph_dir_clear_complete(new_dir);

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
	ceph_dentry(dentry)->time = jiffies;
	ceph_dentry(dentry)->lease_shared_gen = 0;
	spin_unlock(&dentry->d_lock);
}

/*
 * Check if dentry lease is valid.  If not, delete the lease.  Try to
 * renew if the least is more than half up.
 */
static int dentry_lease_is_valid(struct dentry *dentry, unsigned int flags,
				 struct inode *dir)
{
	struct ceph_dentry_info *di;
	struct ceph_mds_session *s;
	int valid = 0;
	u32 gen;
	unsigned long ttl;
	struct ceph_mds_session *session = NULL;
	u32 seq = 0;

	spin_lock(&dentry->d_lock);
	di = ceph_dentry(dentry);
	if (di && di->lease_session) {
		s = di->lease_session;
		spin_lock(&s->s_gen_ttl_lock);
		gen = s->s_cap_gen;
		ttl = s->s_cap_ttl;
		spin_unlock(&s->s_gen_ttl_lock);

		if (di->lease_gen == gen &&
		    time_before(jiffies, di->time) &&
		    time_before(jiffies, ttl)) {
			valid = 1;
			if (di->lease_renew_after &&
			    time_after(jiffies, di->lease_renew_after)) {
				/*
				 * We should renew. If we're in RCU walk mode
				 * though, we can't do that so just return
				 * -ECHILD.
				 */
				if (flags & LOOKUP_RCU) {
					valid = -ECHILD;
				} else {
					session = ceph_get_mds_session(s);
					seq = di->lease_seq;
					di->lease_renew_after = 0;
					di->lease_renew_from = jiffies;
				}
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

	spin_lock(&ci->i_ceph_lock);
	if (ci->i_shared_gen == di->lease_shared_gen)
		valid = __ceph_caps_issued_mask(ci, CEPH_CAP_FILE_SHARED, 1);
	spin_unlock(&ci->i_ceph_lock);
	dout("dir_lease_is_valid dir %p v%u dentry %p v%u = %d\n",
	     dir, (unsigned)ci->i_shared_gen, dentry,
	     (unsigned)di->lease_shared_gen, valid);
	return valid;
}

/*
 * Check if cached dentry can be trusted.
 */
static int ceph_d_revalidate(struct dentry *dentry, unsigned int flags)
{
	int valid = 0;
	struct dentry *parent;
	struct inode *dir;

	if (flags & LOOKUP_RCU) {
		parent = READ_ONCE(dentry->d_parent);
		dir = d_inode_rcu(parent);
		if (!dir)
			return -ECHILD;
	} else {
		parent = dget_parent(dentry);
		dir = d_inode(parent);
	}

	dout("d_revalidate %p '%pd' inode %p offset %lld\n", dentry,
	     dentry, d_inode(dentry), ceph_dentry(dentry)->offset);

	/* always trust cached snapped dentries, snapdir dentry */
	if (ceph_snap(dir) != CEPH_NOSNAP) {
		dout("d_revalidate %p '%pd' inode %p is SNAPPED\n", dentry,
		     dentry, d_inode(dentry));
		valid = 1;
	} else if (d_really_is_positive(dentry) &&
		   ceph_snap(d_inode(dentry)) == CEPH_SNAPDIR) {
		valid = 1;
	} else {
		valid = dentry_lease_is_valid(dentry, flags, dir);
		if (valid == -ECHILD)
			return valid;
		if (valid || dir_lease_is_valid(dir, dentry)) {
			if (d_really_is_positive(dentry))
				valid = ceph_is_any_caps(d_inode(dentry));
			else
				valid = 1;
		}
	}

	if (!valid) {
		struct ceph_mds_client *mdsc =
			ceph_sb_to_client(dir->i_sb)->mdsc;
		struct ceph_mds_request *req;
		int op, err;
		u32 mask;

		if (flags & LOOKUP_RCU)
			return -ECHILD;

		op = ceph_snap(dir) == CEPH_SNAPDIR ?
			CEPH_MDS_OP_LOOKUPSNAP : CEPH_MDS_OP_LOOKUP;
		req = ceph_mdsc_create_request(mdsc, op, USE_ANY_MDS);
		if (!IS_ERR(req)) {
			req->r_dentry = dget(dentry);
			req->r_num_caps = 2;
			req->r_parent = dir;

			mask = CEPH_STAT_CAP_INODE | CEPH_CAP_AUTH_SHARED;
			if (ceph_security_xattr_wanted(dir))
				mask |= CEPH_CAP_XATTR_SHARED;
			req->r_args.getattr.mask = cpu_to_le32(mask);

			err = ceph_mdsc_do_request(mdsc, NULL, req);
			switch (err) {
			case 0:
				if (d_really_is_positive(dentry) &&
				    d_inode(dentry) == req->r_target_inode)
					valid = 1;
				break;
			case -ENOENT:
				if (d_really_is_negative(dentry))
					valid = 1;
				/* Fallthrough */
			default:
				break;
			}
			ceph_mdsc_put_request(req);
			dout("d_revalidate %p lookup result=%d\n",
			     dentry, err);
		}
	}

	dout("d_revalidate %p %s\n", dentry, valid ? "valid" : "invalid");
	if (valid) {
		ceph_dentry_lru_touch(dentry);
	} else {
		ceph_dir_clear_complete(dir);
	}

	if (!(flags & LOOKUP_RCU))
		dput(parent);
	return valid;
}

/*
 * Release our ceph_dentry_info.
 */
static void ceph_d_release(struct dentry *dentry)
{
	struct ceph_dentry_info *di = ceph_dentry(dentry);

	dout("d_release %p\n", dentry);
	ceph_dentry_lru_del(dentry);

	spin_lock(&dentry->d_lock);
	dentry->d_fsdata = NULL;
	spin_unlock(&dentry->d_lock);

	if (di->lease_session)
		ceph_put_mds_session(di->lease_session);
	kmem_cache_free(ceph_dentry_cachep, di);
}

/*
 * When the VFS prunes a dentry from the cache, we need to clear the
 * complete flag on the parent directory.
 *
 * Called under dentry->d_lock.
 */
static void ceph_d_prune(struct dentry *dentry)
{
	dout("ceph_d_prune %p\n", dentry);

	/* do we have a valid parent? */
	if (IS_ROOT(dentry))
		return;

	/* if we are not hashed, we don't affect dir's completeness */
	if (d_unhashed(dentry))
		return;

	if (ceph_snap(d_inode(dentry->d_parent)) == CEPH_SNAPDIR)
		return;

	/*
	 * we hold d_lock, so d_parent is stable, and d_fsdata is never
	 * cleared until d_release
	 */
	ceph_dir_clear_complete(d_inode(dentry->d_parent));
}

/*
 * read() on a dir.  This weird interface hack only works if mounted
 * with '-o dirstat'.
 */
static ssize_t ceph_read_dir(struct file *file, char __user *buf, size_t size,
			     loff_t *ppos)
{
	struct ceph_file_info *cf = file->private_data;
	struct inode *inode = file_inode(file);
	struct ceph_inode_info *ci = ceph_inode(inode);
	int left;
	const int bufsize = 1024;

	if (!ceph_test_mount_opt(ceph_sb_to_client(inode->i_sb), DIRSTAT))
		return -EISDIR;

	if (!cf->dir_info) {
		cf->dir_info = kmalloc(bufsize, GFP_KERNEL);
		if (!cf->dir_info)
			return -ENOMEM;
		cf->dir_info_len =
			snprintf(cf->dir_info, bufsize,
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
 * We maintain a private dentry LRU.
 *
 * FIXME: this needs to be changed to a per-mds lru to be useful.
 */
void ceph_dentry_lru_add(struct dentry *dn)
{
	struct ceph_dentry_info *di = ceph_dentry(dn);
	struct ceph_mds_client *mdsc;

	dout("dentry_lru_add %p %p '%pd'\n", di, dn, dn);
	mdsc = ceph_sb_to_client(dn->d_sb)->mdsc;
	spin_lock(&mdsc->dentry_lru_lock);
	list_add_tail(&di->lru, &mdsc->dentry_lru);
	mdsc->num_dentry++;
	spin_unlock(&mdsc->dentry_lru_lock);
}

void ceph_dentry_lru_touch(struct dentry *dn)
{
	struct ceph_dentry_info *di = ceph_dentry(dn);
	struct ceph_mds_client *mdsc;

	dout("dentry_lru_touch %p %p '%pd' (offset %lld)\n", di, dn, dn,
	     di->offset);
	mdsc = ceph_sb_to_client(dn->d_sb)->mdsc;
	spin_lock(&mdsc->dentry_lru_lock);
	list_move_tail(&di->lru, &mdsc->dentry_lru);
	spin_unlock(&mdsc->dentry_lru_lock);
}

void ceph_dentry_lru_del(struct dentry *dn)
{
	struct ceph_dentry_info *di = ceph_dentry(dn);
	struct ceph_mds_client *mdsc;

	dout("dentry_lru_del %p %p '%pd'\n", di, dn, dn);
	mdsc = ceph_sb_to_client(dn->d_sb)->mdsc;
	spin_lock(&mdsc->dentry_lru_lock);
	list_del_init(&di->lru);
	mdsc->num_dentry--;
	spin_unlock(&mdsc->dentry_lru_lock);
}

/*
 * Return name hash for a given dentry.  This is dependent on
 * the parent directory's hash function.
 */
unsigned ceph_dentry_hash(struct inode *dir, struct dentry *dn)
{
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
	.iterate = ceph_readdir,
	.llseek = ceph_dir_llseek,
	.open = ceph_open,
	.release = ceph_release,
	.unlocked_ioctl = ceph_ioctl,
	.fsync = ceph_fsync,
};

const struct file_operations ceph_snapdir_fops = {
	.iterate = ceph_readdir,
	.llseek = ceph_dir_llseek,
	.open = ceph_open,
	.release = ceph_release,
};

const struct inode_operations ceph_dir_iops = {
	.lookup = ceph_lookup,
	.permission = ceph_permission,
	.getattr = ceph_getattr,
	.setattr = ceph_setattr,
	.listxattr = ceph_listxattr,
	.get_acl = ceph_get_acl,
	.set_acl = ceph_set_acl,
	.mknod = ceph_mknod,
	.symlink = ceph_symlink,
	.mkdir = ceph_mkdir,
	.link = ceph_link,
	.unlink = ceph_unlink,
	.rmdir = ceph_unlink,
	.rename = ceph_rename,
	.create = ceph_create,
	.atomic_open = ceph_atomic_open,
};

const struct inode_operations ceph_snapdir_iops = {
	.lookup = ceph_lookup,
	.permission = ceph_permission,
	.getattr = ceph_getattr,
	.mkdir = ceph_mkdir,
	.rmdir = ceph_unlink,
	.rename = ceph_rename,
};

const struct dentry_operations ceph_dentry_ops = {
	.d_revalidate = ceph_d_revalidate,
	.d_release = ceph_d_release,
	.d_prune = ceph_d_prune,
	.d_init = ceph_d_init,
};
