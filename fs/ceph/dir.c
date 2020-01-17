// SPDX-License-Identifier: GPL-2.0
#include <linux/ceph/ceph_debug.h>

#include <linux/spinlock.h>
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
 * Ceph MDS operations are specified in terms of a base iyes and
 * relative path.  Thus, the client can specify an operation on a
 * specific iyesde (e.g., a getattr due to fstat(2)), or as a path
 * relative to, say, the root directory.
 *
 * Normally, we limit ourselves to strict iyesde ops (yes path component)
 * or dentry operations (a single path component relative to an iyes).  The
 * exception to this is open_root_dentry(), which will open the mount
 * point by name.
 */

const struct dentry_operations ceph_dentry_ops;

static bool __dentry_lease_is_valid(struct ceph_dentry_info *di);
static int __dir_lease_try_check(const struct dentry *dentry);

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
	INIT_LIST_HEAD(&di->lease_list);
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
 * make yeste of the last dentry we read, so we can
 * continue at the same lexicographical point,
 * regardless of what dir changes take place on the
 * server.
 */
static int yeste_last_dentry(struct ceph_dir_file_info *dfi, const char *name,
		            int len, unsigned next_offset)
{
	char *buf = kmalloc(len+1, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	kfree(dfi->last_name);
	dfi->last_name = buf;
	memcpy(dfi->last_name, name, len);
	dfi->last_name[len] = 0;
	dfi->next_offset = next_offset;
	dout("yeste_last_dentry '%s'\n", dfi->last_name);
	return 0;
}


static struct dentry *
__dcache_find_get_entry(struct dentry *parent, u64 idx,
			struct ceph_readdir_cache_control *cache_ctl)
{
	struct iyesde *dir = d_iyesde(parent);
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
			dout(" page %lu yest found\n", ptr_pgoff);
			return ERR_PTR(-EAGAIN);
		}
		/* reading/filling the cache are serialized by
		   i_mutex, yes need to use page lock */
		unlock_page(cache_ctl->page);
		cache_ctl->dentries = kmap(cache_ctl->page);
	}

	cache_ctl->index = idx & idx_mask;

	rcu_read_lock();
	spin_lock(&parent->d_lock);
	/* check i_size again here, because empty directory can be
	 * marked as complete while yest holding the i_mutex. */
	if (ceph_dir_is_complete_ordered(dir) && ptr_pos < i_size_read(dir))
		dentry = cache_ctl->dentries[cache_ctl->index];
	else
		dentry = NULL;
	spin_unlock(&parent->d_lock);
	if (dentry && !lockref_get_yest_dead(&dentry->d_lockref))
		dentry = NULL;
	rcu_read_unlock();
	return dentry ? : ERR_PTR(-EAGAIN);
}

/*
 * When possible, we try to satisfy a readdir by peeking at the
 * dcache.  We make this work by carefully ordering dentries on
 * d_child when we initially get results back from the MDS, and
 * falling back to a "yesrmal" sync readdir if any dentries in the dir
 * are dropped.
 *
 * Complete dir indicates that we have all dentries in the dir.  It is
 * defined IFF we hold CEPH_CAP_FILE_SHARED (which will be revoked by
 * the MDS if/when the directory is modified).
 */
static int __dcache_readdir(struct file *file,  struct dir_context *ctx,
			    int shared_gen)
{
	struct ceph_dir_file_info *dfi = file->private_data;
	struct dentry *parent = file->f_path.dentry;
	struct iyesde *dir = d_iyesde(parent);
	struct dentry *dentry, *last = NULL;
	struct ceph_dentry_info *di;
	struct ceph_readdir_cache_control cache_ctl = {};
	u64 idx = 0;
	int err = 0;

	dout("__dcache_readdir %p v%u at %llx\n", dir, (unsigned)shared_gen, ctx->pos);

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
			dfi->file_info.flags |= CEPH_F_ATEND;
			err = 0;
			break;
		}
		if (IS_ERR(dentry)) {
			err = PTR_ERR(dentry);
			goto out;
		}

		spin_lock(&dentry->d_lock);
		di = ceph_dentry(dentry);
		if (d_unhashed(dentry) ||
		    d_really_is_negative(dentry) ||
		    di->lease_shared_gen != shared_gen) {
			spin_unlock(&dentry->d_lock);
			dput(dentry);
			err = -EAGAIN;
			goto out;
		}
		if (fpos_cmp(ctx->pos, di->offset) <= 0) {
			__ceph_dentry_dir_lease_touch(di);
			emit_dentry = true;
		}
		spin_unlock(&dentry->d_lock);

		if (emit_dentry) {
			dout(" %llx dentry %p %pd %p\n", di->offset,
			     dentry, dentry, d_iyesde(dentry));
			ctx->pos = di->offset;
			if (!dir_emit(ctx, dentry->d_name.name,
				      dentry->d_name.len,
				      ceph_translate_iyes(dentry->d_sb,
							 d_iyesde(dentry)->i_iyes),
				      d_iyesde(dentry)->i_mode >> 12)) {
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
		ret = yeste_last_dentry(dfi, last->d_name.name, last->d_name.len,
				       fpos_off(di->offset) + 1);
		if (ret < 0)
			err = ret;
		dput(last);
		/* last_name yes longer match cache index */
		if (dfi->readdir_cache_idx >= 0) {
			dfi->readdir_cache_idx = -1;
			dfi->dir_release_count = 0;
		}
	}
	return err;
}

static bool need_send_readdir(struct ceph_dir_file_info *dfi, loff_t pos)
{
	if (!dfi->last_readdir)
		return true;
	if (is_hash_order(pos))
		return !ceph_frag_contains_value(dfi->frag, fpos_hash(pos));
	else
		return dfi->frag != fpos_frag(pos);
}

static int ceph_readdir(struct file *file, struct dir_context *ctx)
{
	struct ceph_dir_file_info *dfi = file->private_data;
	struct iyesde *iyesde = file_iyesde(file);
	struct ceph_iyesde_info *ci = ceph_iyesde(iyesde);
	struct ceph_fs_client *fsc = ceph_iyesde_to_client(iyesde);
	struct ceph_mds_client *mdsc = fsc->mdsc;
	int i;
	int err;
	unsigned frag = -1;
	struct ceph_mds_reply_info_parsed *rinfo;

	dout("readdir %p file %p pos %llx\n", iyesde, file, ctx->pos);
	if (dfi->file_info.flags & CEPH_F_ATEND)
		return 0;

	/* always start with . and .. */
	if (ctx->pos == 0) {
		dout("readdir off 0 -> '.'\n");
		if (!dir_emit(ctx, ".", 1, 
			    ceph_translate_iyes(iyesde->i_sb, iyesde->i_iyes),
			    iyesde->i_mode >> 12))
			return 0;
		ctx->pos = 1;
	}
	if (ctx->pos == 1) {
		iyes_t iyes = parent_iyes(file->f_path.dentry);
		dout("readdir off 1 -> '..'\n");
		if (!dir_emit(ctx, "..", 2,
			    ceph_translate_iyes(iyesde->i_sb, iyes),
			    iyesde->i_mode >> 12))
			return 0;
		ctx->pos = 2;
	}

	/* can we use the dcache? */
	spin_lock(&ci->i_ceph_lock);
	if (ceph_test_mount_opt(fsc, DCACHE) &&
	    !ceph_test_mount_opt(fsc, NOASYNCREADDIR) &&
	    ceph_snap(iyesde) != CEPH_SNAPDIR &&
	    __ceph_dir_is_complete_ordered(ci) &&
	    __ceph_caps_issued_mask(ci, CEPH_CAP_FILE_SHARED, 1)) {
		int shared_gen = atomic_read(&ci->i_shared_gen);
		spin_unlock(&ci->i_ceph_lock);
		err = __dcache_readdir(file, ctx, shared_gen);
		if (err != -EAGAIN)
			return err;
	} else {
		spin_unlock(&ci->i_ceph_lock);
	}

	/* proceed with a yesrmal readdir */
more:
	/* do we have the correct frag content buffered? */
	if (need_send_readdir(dfi, ctx->pos)) {
		struct ceph_mds_request *req;
		int op = ceph_snap(iyesde) == CEPH_SNAPDIR ?
			CEPH_MDS_OP_LSSNAP : CEPH_MDS_OP_READDIR;

		/* discard old result, if any */
		if (dfi->last_readdir) {
			ceph_mdsc_put_request(dfi->last_readdir);
			dfi->last_readdir = NULL;
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
		     ceph_viyesp(iyesde), frag, dfi->last_name);
		req = ceph_mdsc_create_request(mdsc, op, USE_AUTH_MDS);
		if (IS_ERR(req))
			return PTR_ERR(req);
		err = ceph_alloc_readdir_reply_buffer(req, iyesde);
		if (err) {
			ceph_mdsc_put_request(req);
			return err;
		}
		/* hints to request -> mds selection code */
		req->r_direct_mode = USE_AUTH_MDS;
		if (op == CEPH_MDS_OP_READDIR) {
			req->r_direct_hash = ceph_frag_value(frag);
			__set_bit(CEPH_MDS_R_DIRECT_IS_HASH, &req->r_req_flags);
			req->r_iyesde_drop = CEPH_CAP_FILE_EXCL;
		}
		if (dfi->last_name) {
			req->r_path2 = kstrdup(dfi->last_name, GFP_KERNEL);
			if (!req->r_path2) {
				ceph_mdsc_put_request(req);
				return -ENOMEM;
			}
		} else if (is_hash_order(ctx->pos)) {
			req->r_args.readdir.offset_hash =
				cpu_to_le32(fpos_hash(ctx->pos));
		}

		req->r_dir_release_cnt = dfi->dir_release_count;
		req->r_dir_ordered_cnt = dfi->dir_ordered_count;
		req->r_readdir_cache_idx = dfi->readdir_cache_idx;
		req->r_readdir_offset = dfi->next_offset;
		req->r_args.readdir.frag = cpu_to_le32(frag);
		req->r_args.readdir.flags =
				cpu_to_le16(CEPH_READDIR_REPLY_BITFLAGS);

		req->r_iyesde = iyesde;
		ihold(iyesde);
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
				dfi->next_offset = req->r_readdir_offset;
				/* adjust ctx->pos to beginning of frag */
				ctx->pos = ceph_make_fpos(frag,
							  dfi->next_offset,
							  false);
			}
		}

		dfi->frag = frag;
		dfi->last_readdir = req;

		if (test_bit(CEPH_MDS_R_DID_PREPOPULATE, &req->r_req_flags)) {
			dfi->readdir_cache_idx = req->r_readdir_cache_idx;
			if (dfi->readdir_cache_idx < 0) {
				/* preclude from marking dir ordered */
				dfi->dir_ordered_count = 0;
			} else if (ceph_frag_is_leftmost(frag) &&
				   dfi->next_offset == 2) {
				/* yeste dir version at start of readdir so
				 * we can tell if any dentries get dropped */
				dfi->dir_release_count = req->r_dir_release_cnt;
				dfi->dir_ordered_count = req->r_dir_ordered_cnt;
			}
		} else {
			dout("readdir !did_prepopulate\n");
			/* disable readdir cache */
			dfi->readdir_cache_idx = -1;
			/* preclude from marking dir complete */
			dfi->dir_release_count = 0;
		}

		/* yeste next offset and last dentry name */
		if (rinfo->dir_nr > 0) {
			struct ceph_mds_reply_dir_entry *rde =
					rinfo->dir_entries + (rinfo->dir_nr-1);
			unsigned next_offset = req->r_reply_info.dir_end ?
					2 : (fpos_off(rde->offset) + 1);
			err = yeste_last_dentry(dfi, rde->name, rde->name_len,
					       next_offset);
			if (err)
				return err;
		} else if (req->r_reply_info.dir_end) {
			dfi->next_offset = 2;
			/* keep last name */
		}
	}

	rinfo = &dfi->last_readdir->r_reply_info;
	dout("readdir frag %x num %d pos %llx chunk first %llx\n",
	     dfi->frag, rinfo->dir_nr, ctx->pos,
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
		struct ceph_viyes viyes;
		iyes_t iyes;
		u32 ftype;

		BUG_ON(rde->offset < ctx->pos);

		ctx->pos = rde->offset;
		dout("readdir (%d/%d) -> %llx '%.*s' %p\n",
		     i, rinfo->dir_nr, ctx->pos,
		     rde->name_len, rde->name, &rde->iyesde.in);

		BUG_ON(!rde->iyesde.in);
		ftype = le32_to_cpu(rde->iyesde.in->mode) >> 12;
		viyes.iyes = le64_to_cpu(rde->iyesde.in->iyes);
		viyes.snap = le64_to_cpu(rde->iyesde.in->snapid);
		iyes = ceph_viyes_to_iyes(viyes);

		if (!dir_emit(ctx, rde->name, rde->name_len,
			      ceph_translate_iyes(iyesde->i_sb, iyes), ftype)) {
			dout("filldir stopping us...\n");
			return 0;
		}
		ctx->pos++;
	}

	ceph_mdsc_put_request(dfi->last_readdir);
	dfi->last_readdir = NULL;

	if (dfi->next_offset > 2) {
		frag = dfi->frag;
		goto more;
	}

	/* more frags? */
	if (!ceph_frag_is_rightmost(dfi->frag)) {
		frag = ceph_frag_next(dfi->frag);
		if (is_hash_order(ctx->pos)) {
			loff_t new_pos = ceph_make_fpos(ceph_frag_value(frag),
							dfi->next_offset, true);
			if (new_pos > ctx->pos)
				ctx->pos = new_pos;
			/* keep last_name */
		} else {
			ctx->pos = ceph_make_fpos(frag, dfi->next_offset,
							false);
			kfree(dfi->last_name);
			dfi->last_name = NULL;
		}
		dout("readdir next frag is %x\n", frag);
		goto more;
	}
	dfi->file_info.flags |= CEPH_F_ATEND;

	/*
	 * if dir_release_count still matches the dir, yes dentries
	 * were released during the whole readdir, and we should have
	 * the complete dir contents in our cache.
	 */
	if (atomic64_read(&ci->i_release_count) ==
					dfi->dir_release_count) {
		spin_lock(&ci->i_ceph_lock);
		if (dfi->dir_ordered_count ==
				atomic64_read(&ci->i_ordered_count)) {
			dout(" marking %p complete and ordered\n", iyesde);
			/* use i_size to track number of entries in
			 * readdir cache */
			BUG_ON(dfi->readdir_cache_idx < 0);
			i_size_write(iyesde, dfi->readdir_cache_idx *
				     sizeof(struct dentry*));
		} else {
			dout(" marking %p complete\n", iyesde);
		}
		__ceph_dir_set_complete(ci, dfi->dir_release_count,
					dfi->dir_ordered_count);
		spin_unlock(&ci->i_ceph_lock);
	}

	dout("readdir %p file %p done.\n", iyesde, file);
	return 0;
}

static void reset_readdir(struct ceph_dir_file_info *dfi)
{
	if (dfi->last_readdir) {
		ceph_mdsc_put_request(dfi->last_readdir);
		dfi->last_readdir = NULL;
	}
	kfree(dfi->last_name);
	dfi->last_name = NULL;
	dfi->dir_release_count = 0;
	dfi->readdir_cache_idx = -1;
	dfi->next_offset = 2;  /* compensate for . and .. */
	dfi->file_info.flags &= ~CEPH_F_ATEND;
}

/*
 * discard buffered readdir content on seekdir(0), or seek to new frag,
 * or seek prior to current chunk
 */
static bool need_reset_readdir(struct ceph_dir_file_info *dfi, loff_t new_pos)
{
	struct ceph_mds_reply_info_parsed *rinfo;
	loff_t chunk_offset;
	if (new_pos == 0)
		return true;
	if (is_hash_order(new_pos)) {
		/* yes need to reset last_name for a forward seek when
		 * dentries are sotred in hash order */
	} else if (dfi->frag != fpos_frag(new_pos)) {
		return true;
	}
	rinfo = dfi->last_readdir ? &dfi->last_readdir->r_reply_info : NULL;
	if (!rinfo || !rinfo->dir_nr)
		return true;
	chunk_offset = rinfo->dir_entries[0].offset;
	return new_pos < chunk_offset ||
	       is_hash_order(new_pos) != is_hash_order(chunk_offset);
}

static loff_t ceph_dir_llseek(struct file *file, loff_t offset, int whence)
{
	struct ceph_dir_file_info *dfi = file->private_data;
	struct iyesde *iyesde = file->f_mapping->host;
	loff_t retval;

	iyesde_lock(iyesde);
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
		if (need_reset_readdir(dfi, offset)) {
			dout("dir_llseek dropping %p content\n", file);
			reset_readdir(dfi);
		} else if (is_hash_order(offset) && offset > file->f_pos) {
			/* for hash offset, we don't kyesw if a forward seek
			 * is within same frag */
			dfi->dir_release_count = 0;
			dfi->readdir_cache_idx = -1;
		}

		if (offset != file->f_pos) {
			file->f_pos = offset;
			file->f_version = 0;
			dfi->file_info.flags &= ~CEPH_F_ATEND;
		}
		retval = offset;
	}
out:
	iyesde_unlock(iyesde);
	return retval;
}

/*
 * Handle lookups for the hidden .snap directory.
 */
int ceph_handle_snapdir(struct ceph_mds_request *req,
			struct dentry *dentry, int err)
{
	struct ceph_fs_client *fsc = ceph_sb_to_client(dentry->d_sb);
	struct iyesde *parent = d_iyesde(dentry->d_parent); /* we hold i_mutex */

	/* .snap dir? */
	if (err == -ENOENT &&
	    ceph_snap(parent) == CEPH_NOSNAP &&
	    strcmp(dentry->d_name.name,
		   fsc->mount_options->snapdir_name) == 0) {
		struct iyesde *iyesde = ceph_get_snapdir(parent);
		dout("ENOENT on snapdir %p '%pd', linking to snapdir %p\n",
		     dentry, dentry, iyesde);
		BUG_ON(!d_unhashed(dentry));
		d_add(dentry, iyesde);
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
 * yes trace (which it may do, at its discretion, e.g., if it doesn't
 * care to issue a lease on the negative dentry).
 */
struct dentry *ceph_finish_lookup(struct ceph_mds_request *req,
				  struct dentry *dentry, int err)
{
	if (err == -ENOENT) {
		/* yes trace? */
		err = 0;
		if (!req->r_reply_info.head->is_dentry) {
			dout("ENOENT and yes trace, dentry %p iyesde %p\n",
			     dentry, d_iyesde(dentry));
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

static bool is_root_ceph_dentry(struct iyesde *iyesde, struct dentry *dentry)
{
	return ceph_iyes(iyesde) == CEPH_INO_ROOT &&
		strncmp(dentry->d_name.name, ".ceph", 5) == 0;
}

/*
 * Look up a single dir entry.  If there is a lookup intent, inform
 * the MDS so that it gets our 'caps wanted' value in a single op.
 */
static struct dentry *ceph_lookup(struct iyesde *dir, struct dentry *dentry,
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
		struct ceph_iyesde_info *ci = ceph_iyesde(dir);
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
			di->lease_shared_gen = atomic_read(&ci->i_shared_gen);
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
 * If we do a create but get yes trace back from the MDS, follow up with
 * a lookup (the VFS expects us to link up the provided dentry).
 */
int ceph_handle_yestrace_create(struct iyesde *dir, struct dentry *dentry)
{
	struct dentry *result = ceph_lookup(dir, dentry, 0);

	if (result && !IS_ERR(result)) {
		/*
		 * We created the item, then did a lookup, and found
		 * it was already linked to ayesther iyesde we already
		 * had in our cache (and thus got spliced). To yest
		 * confuse VFS (especially when iyesde is a directory),
		 * we don't link our dentry to that iyesde, return an
		 * error instead.
		 *
		 * This event should be rare and it happens only when
		 * we talk to old MDS. Recent MDS does yest send traceless
		 * reply for request that creates new iyesde.
		 */
		d_drop(result);
		return -ESTALE;
	}
	return PTR_ERR(result);
}

static int ceph_mkyesd(struct iyesde *dir, struct dentry *dentry,
		      umode_t mode, dev_t rdev)
{
	struct ceph_fs_client *fsc = ceph_sb_to_client(dir->i_sb);
	struct ceph_mds_client *mdsc = fsc->mdsc;
	struct ceph_mds_request *req;
	struct ceph_acl_sec_ctx as_ctx = {};
	int err;

	if (ceph_snap(dir) != CEPH_NOSNAP)
		return -EROFS;

	if (ceph_quota_is_max_files_exceeded(dir)) {
		err = -EDQUOT;
		goto out;
	}

	err = ceph_pre_init_acls(dir, &mode, &as_ctx);
	if (err < 0)
		goto out;
	err = ceph_security_init_secctx(dentry, mode, &as_ctx);
	if (err < 0)
		goto out;

	dout("mkyesd in dir %p dentry %p mode 0%ho rdev %d\n",
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
	req->r_args.mkyesd.mode = cpu_to_le32(mode);
	req->r_args.mkyesd.rdev = cpu_to_le32(rdev);
	req->r_dentry_drop = CEPH_CAP_FILE_SHARED | CEPH_CAP_AUTH_EXCL;
	req->r_dentry_unless = CEPH_CAP_FILE_EXCL;
	if (as_ctx.pagelist) {
		req->r_pagelist = as_ctx.pagelist;
		as_ctx.pagelist = NULL;
	}
	err = ceph_mdsc_do_request(mdsc, dir, req);
	if (!err && !req->r_reply_info.head->is_dentry)
		err = ceph_handle_yestrace_create(dir, dentry);
	ceph_mdsc_put_request(req);
out:
	if (!err)
		ceph_init_iyesde_acls(d_iyesde(dentry), &as_ctx);
	else
		d_drop(dentry);
	ceph_release_acl_sec_ctx(&as_ctx);
	return err;
}

static int ceph_create(struct iyesde *dir, struct dentry *dentry, umode_t mode,
		       bool excl)
{
	return ceph_mkyesd(dir, dentry, mode, 0);
}

static int ceph_symlink(struct iyesde *dir, struct dentry *dentry,
			    const char *dest)
{
	struct ceph_fs_client *fsc = ceph_sb_to_client(dir->i_sb);
	struct ceph_mds_client *mdsc = fsc->mdsc;
	struct ceph_mds_request *req;
	struct ceph_acl_sec_ctx as_ctx = {};
	int err;

	if (ceph_snap(dir) != CEPH_NOSNAP)
		return -EROFS;

	if (ceph_quota_is_max_files_exceeded(dir)) {
		err = -EDQUOT;
		goto out;
	}

	err = ceph_security_init_secctx(dentry, S_IFLNK | 0777, &as_ctx);
	if (err < 0)
		goto out;

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
	req->r_dentry_drop = CEPH_CAP_FILE_SHARED | CEPH_CAP_AUTH_EXCL;
	req->r_dentry_unless = CEPH_CAP_FILE_EXCL;
	err = ceph_mdsc_do_request(mdsc, dir, req);
	if (!err && !req->r_reply_info.head->is_dentry)
		err = ceph_handle_yestrace_create(dir, dentry);
	ceph_mdsc_put_request(req);
out:
	if (err)
		d_drop(dentry);
	ceph_release_acl_sec_ctx(&as_ctx);
	return err;
}

static int ceph_mkdir(struct iyesde *dir, struct dentry *dentry, umode_t mode)
{
	struct ceph_fs_client *fsc = ceph_sb_to_client(dir->i_sb);
	struct ceph_mds_client *mdsc = fsc->mdsc;
	struct ceph_mds_request *req;
	struct ceph_acl_sec_ctx as_ctx = {};
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

	if (op == CEPH_MDS_OP_MKDIR &&
	    ceph_quota_is_max_files_exceeded(dir)) {
		err = -EDQUOT;
		goto out;
	}

	mode |= S_IFDIR;
	err = ceph_pre_init_acls(dir, &mode, &as_ctx);
	if (err < 0)
		goto out;
	err = ceph_security_init_secctx(dentry, mode, &as_ctx);
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
	req->r_dentry_drop = CEPH_CAP_FILE_SHARED | CEPH_CAP_AUTH_EXCL;
	req->r_dentry_unless = CEPH_CAP_FILE_EXCL;
	if (as_ctx.pagelist) {
		req->r_pagelist = as_ctx.pagelist;
		as_ctx.pagelist = NULL;
	}
	err = ceph_mdsc_do_request(mdsc, dir, req);
	if (!err &&
	    !req->r_reply_info.head->is_target &&
	    !req->r_reply_info.head->is_dentry)
		err = ceph_handle_yestrace_create(dir, dentry);
	ceph_mdsc_put_request(req);
out:
	if (!err)
		ceph_init_iyesde_acls(d_iyesde(dentry), &as_ctx);
	else
		d_drop(dentry);
	ceph_release_acl_sec_ctx(&as_ctx);
	return err;
}

static int ceph_link(struct dentry *old_dentry, struct iyesde *dir,
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
	/* release LINK_SHARED on source iyesde (mds will lock it) */
	req->r_old_iyesde_drop = CEPH_CAP_LINK_SHARED | CEPH_CAP_LINK_EXCL;
	err = ceph_mdsc_do_request(mdsc, dir, req);
	if (err) {
		d_drop(dentry);
	} else if (!req->r_reply_info.head->is_dentry) {
		ihold(d_iyesde(old_dentry));
		d_instantiate(dentry, d_iyesde(old_dentry));
	}
	ceph_mdsc_put_request(req);
	return err;
}

/*
 * rmdir and unlink are differ only by the metadata op code
 */
static int ceph_unlink(struct iyesde *dir, struct dentry *dentry)
{
	struct ceph_fs_client *fsc = ceph_sb_to_client(dir->i_sb);
	struct ceph_mds_client *mdsc = fsc->mdsc;
	struct iyesde *iyesde = d_iyesde(dentry);
	struct ceph_mds_request *req;
	int err = -EROFS;
	int op;

	if (ceph_snap(dir) == CEPH_SNAPDIR) {
		/* rmdir .snap/foo is RMSNAP */
		dout("rmsnap dir %p '%pd' dn %p\n", dir, dentry, dentry);
		op = CEPH_MDS_OP_RMSNAP;
	} else if (ceph_snap(dir) == CEPH_NOSNAP) {
		dout("unlink/rmdir dir %p dn %p iyesde %p\n",
		     dir, dentry, iyesde);
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
	req->r_iyesde_drop = ceph_drop_caps_for_unlink(iyesde);
	err = ceph_mdsc_do_request(mdsc, dir, req);
	if (!err && !req->r_reply_info.head->is_dentry)
		d_delete(dentry);
	ceph_mdsc_put_request(req);
out:
	return err;
}

static int ceph_rename(struct iyesde *old_dir, struct dentry *old_dentry,
		       struct iyesde *new_dir, struct dentry *new_dentry,
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
	/* don't allow cross-quota renames */
	if ((old_dir != new_dir) &&
	    (!ceph_quota_is_same_realm(old_dir, new_dir)))
		return -EXDEV;

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
	/* release LINK_RDCACHE on source iyesde (mds will lock it) */
	req->r_old_iyesde_drop = CEPH_CAP_LINK_SHARED | CEPH_CAP_LINK_EXCL;
	if (d_really_is_positive(new_dentry)) {
		req->r_iyesde_drop =
			ceph_drop_caps_for_unlink(d_iyesde(new_dentry));
	}
	err = ceph_mdsc_do_request(mdsc, old_dir, req);
	if (!err && !req->r_reply_info.head->is_dentry) {
		/*
		 * Normally d_move() is done by fill_trace (called by
		 * do_request, above).  If there is yes trace, we need
		 * to do it here.
		 */
		d_move(old_dentry, new_dentry);
	}
	ceph_mdsc_put_request(req);
	return err;
}

/*
 * Move dentry to tail of mdsc->dentry_leases list when lease is updated.
 * Leases at front of the list will expire first. (Assume all leases have
 * similar duration)
 *
 * Called under dentry->d_lock.
 */
void __ceph_dentry_lease_touch(struct ceph_dentry_info *di)
{
	struct dentry *dn = di->dentry;
	struct ceph_mds_client *mdsc;

	dout("dentry_lease_touch %p %p '%pd'\n", di, dn, dn);

	di->flags |= CEPH_DENTRY_LEASE_LIST;
	if (di->flags & CEPH_DENTRY_SHRINK_LIST) {
		di->flags |= CEPH_DENTRY_REFERENCED;
		return;
	}

	mdsc = ceph_sb_to_client(dn->d_sb)->mdsc;
	spin_lock(&mdsc->dentry_list_lock);
	list_move_tail(&di->lease_list, &mdsc->dentry_leases);
	spin_unlock(&mdsc->dentry_list_lock);
}

static void __dentry_dir_lease_touch(struct ceph_mds_client* mdsc,
				     struct ceph_dentry_info *di)
{
	di->flags &= ~(CEPH_DENTRY_LEASE_LIST | CEPH_DENTRY_REFERENCED);
	di->lease_gen = 0;
	di->time = jiffies;
	list_move_tail(&di->lease_list, &mdsc->dentry_dir_leases);
}

/*
 * When dir lease is used, add dentry to tail of mdsc->dentry_dir_leases
 * list if it's yest in the list, otherwise set 'referenced' flag.
 *
 * Called under dentry->d_lock.
 */
void __ceph_dentry_dir_lease_touch(struct ceph_dentry_info *di)
{
	struct dentry *dn = di->dentry;
	struct ceph_mds_client *mdsc;

	dout("dentry_dir_lease_touch %p %p '%pd' (offset %lld)\n",
	     di, dn, dn, di->offset);

	if (!list_empty(&di->lease_list)) {
		if (di->flags & CEPH_DENTRY_LEASE_LIST) {
			/* don't remove dentry from dentry lease list
			 * if its lease is valid */
			if (__dentry_lease_is_valid(di))
				return;
		} else {
			di->flags |= CEPH_DENTRY_REFERENCED;
			return;
		}
	}

	if (di->flags & CEPH_DENTRY_SHRINK_LIST) {
		di->flags |= CEPH_DENTRY_REFERENCED;
		di->flags &= ~CEPH_DENTRY_LEASE_LIST;
		return;
	}

	mdsc = ceph_sb_to_client(dn->d_sb)->mdsc;
	spin_lock(&mdsc->dentry_list_lock);
	__dentry_dir_lease_touch(mdsc, di),
	spin_unlock(&mdsc->dentry_list_lock);
}

static void __dentry_lease_unlist(struct ceph_dentry_info *di)
{
	struct ceph_mds_client *mdsc;
	if (di->flags & CEPH_DENTRY_SHRINK_LIST)
		return;
	if (list_empty(&di->lease_list))
		return;

	mdsc = ceph_sb_to_client(di->dentry->d_sb)->mdsc;
	spin_lock(&mdsc->dentry_list_lock);
	list_del_init(&di->lease_list);
	spin_unlock(&mdsc->dentry_list_lock);
}

enum {
	KEEP	= 0,
	DELETE	= 1,
	TOUCH	= 2,
	STOP	= 4,
};

struct ceph_lease_walk_control {
	bool dir_lease;
	bool expire_dir_lease;
	unsigned long nr_to_scan;
	unsigned long dir_lease_ttl;
};

static unsigned long
__dentry_leases_walk(struct ceph_mds_client *mdsc,
		     struct ceph_lease_walk_control *lwc,
		     int (*check)(struct dentry*, void*))
{
	struct ceph_dentry_info *di, *tmp;
	struct dentry *dentry, *last = NULL;
	struct list_head* list;
        LIST_HEAD(dispose);
	unsigned long freed = 0;
	int ret = 0;

	list = lwc->dir_lease ? &mdsc->dentry_dir_leases : &mdsc->dentry_leases;
	spin_lock(&mdsc->dentry_list_lock);
	list_for_each_entry_safe(di, tmp, list, lease_list) {
		if (!lwc->nr_to_scan)
			break;
		--lwc->nr_to_scan;

		dentry = di->dentry;
		if (last == dentry)
			break;

		if (!spin_trylock(&dentry->d_lock))
			continue;

		if (__lockref_is_dead(&dentry->d_lockref)) {
			list_del_init(&di->lease_list);
			goto next;
		}

		ret = check(dentry, lwc);
		if (ret & TOUCH) {
			/* move it into tail of dir lease list */
			__dentry_dir_lease_touch(mdsc, di);
			if (!last)
				last = dentry;
		}
		if (ret & DELETE) {
			/* stale lease */
			di->flags &= ~CEPH_DENTRY_REFERENCED;
			if (dentry->d_lockref.count > 0) {
				/* update_dentry_lease() will re-add
				 * it to lease list, or
				 * ceph_d_delete() will return 1 when
				 * last reference is dropped */
				list_del_init(&di->lease_list);
			} else {
				di->flags |= CEPH_DENTRY_SHRINK_LIST;
				list_move_tail(&di->lease_list, &dispose);
				dget_dlock(dentry);
			}
		}
next:
		spin_unlock(&dentry->d_lock);
		if (ret & STOP)
			break;
	}
	spin_unlock(&mdsc->dentry_list_lock);

	while (!list_empty(&dispose)) {
		di = list_first_entry(&dispose, struct ceph_dentry_info,
				      lease_list);
		dentry = di->dentry;
		spin_lock(&dentry->d_lock);

		list_del_init(&di->lease_list);
		di->flags &= ~CEPH_DENTRY_SHRINK_LIST;
		if (di->flags & CEPH_DENTRY_REFERENCED) {
			spin_lock(&mdsc->dentry_list_lock);
			if (di->flags & CEPH_DENTRY_LEASE_LIST) {
				list_add_tail(&di->lease_list,
					      &mdsc->dentry_leases);
			} else {
				__dentry_dir_lease_touch(mdsc, di);
			}
			spin_unlock(&mdsc->dentry_list_lock);
		} else {
			freed++;
		}

		spin_unlock(&dentry->d_lock);
		/* ceph_d_delete() does the trick */
		dput(dentry);
	}
	return freed;
}

static int __dentry_lease_check(struct dentry *dentry, void *arg)
{
	struct ceph_dentry_info *di = ceph_dentry(dentry);
	int ret;

	if (__dentry_lease_is_valid(di))
		return STOP;
	ret = __dir_lease_try_check(dentry);
	if (ret == -EBUSY)
		return KEEP;
	if (ret > 0)
		return TOUCH;
	return DELETE;
}

static int __dir_lease_check(struct dentry *dentry, void *arg)
{
	struct ceph_lease_walk_control *lwc = arg;
	struct ceph_dentry_info *di = ceph_dentry(dentry);

	int ret = __dir_lease_try_check(dentry);
	if (ret == -EBUSY)
		return KEEP;
	if (ret > 0) {
		if (time_before(jiffies, di->time + lwc->dir_lease_ttl))
			return STOP;
		/* Move dentry to tail of dir lease list if we don't want
		 * to delete it. So dentries in the list are checked in a
		 * round robin manner */
		if (!lwc->expire_dir_lease)
			return TOUCH;
		if (dentry->d_lockref.count > 0 ||
		    (di->flags & CEPH_DENTRY_REFERENCED))
			return TOUCH;
		/* invalidate dir lease */
		di->lease_shared_gen = 0;
	}
	return DELETE;
}

int ceph_trim_dentries(struct ceph_mds_client *mdsc)
{
	struct ceph_lease_walk_control lwc;
	unsigned long count;
	unsigned long freed;

	spin_lock(&mdsc->caps_list_lock);
        if (mdsc->caps_use_max > 0 &&
            mdsc->caps_use_count > mdsc->caps_use_max)
		count = mdsc->caps_use_count - mdsc->caps_use_max;
	else
		count = 0;
        spin_unlock(&mdsc->caps_list_lock);

	lwc.dir_lease = false;
	lwc.nr_to_scan  = CEPH_CAPS_PER_RELEASE * 2;
	freed = __dentry_leases_walk(mdsc, &lwc, __dentry_lease_check);
	if (!lwc.nr_to_scan) /* more invalid leases */
		return -EAGAIN;

	if (lwc.nr_to_scan < CEPH_CAPS_PER_RELEASE)
		lwc.nr_to_scan = CEPH_CAPS_PER_RELEASE;

	lwc.dir_lease = true;
	lwc.expire_dir_lease = freed < count;
	lwc.dir_lease_ttl = mdsc->fsc->mount_options->caps_wanted_delay_max * HZ;
	freed +=__dentry_leases_walk(mdsc, &lwc, __dir_lease_check);
	if (!lwc.nr_to_scan) /* more to check */
		return -EAGAIN;

	return freed > 0 ? 1 : 0;
}

/*
 * Ensure a dentry lease will yes longer revalidate.
 */
void ceph_invalidate_dentry_lease(struct dentry *dentry)
{
	struct ceph_dentry_info *di = ceph_dentry(dentry);
	spin_lock(&dentry->d_lock);
	di->time = jiffies;
	di->lease_shared_gen = 0;
	__dentry_lease_unlist(di);
	spin_unlock(&dentry->d_lock);
}

/*
 * Check if dentry lease is valid.  If yest, delete the lease.  Try to
 * renew if the least is more than half up.
 */
static bool __dentry_lease_is_valid(struct ceph_dentry_info *di)
{
	struct ceph_mds_session *session;

	if (!di->lease_gen)
		return false;

	session = di->lease_session;
	if (session) {
		u32 gen;
		unsigned long ttl;

		spin_lock(&session->s_gen_ttl_lock);
		gen = session->s_cap_gen;
		ttl = session->s_cap_ttl;
		spin_unlock(&session->s_gen_ttl_lock);

		if (di->lease_gen == gen &&
		    time_before(jiffies, ttl) &&
		    time_before(jiffies, di->time))
			return true;
	}
	di->lease_gen = 0;
	return false;
}

static int dentry_lease_is_valid(struct dentry *dentry, unsigned int flags)
{
	struct ceph_dentry_info *di;
	struct ceph_mds_session *session = NULL;
	u32 seq = 0;
	int valid = 0;

	spin_lock(&dentry->d_lock);
	di = ceph_dentry(dentry);
	if (di && __dentry_lease_is_valid(di)) {
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
				session = ceph_get_mds_session(di->lease_session);
				seq = di->lease_seq;
				di->lease_renew_after = 0;
				di->lease_renew_from = jiffies;
			}
		}
	}
	spin_unlock(&dentry->d_lock);

	if (session) {
		ceph_mdsc_lease_send_msg(session, dentry,
					 CEPH_MDS_LEASE_RENEW, seq);
		ceph_put_mds_session(session);
	}
	dout("dentry_lease_is_valid - dentry %p = %d\n", dentry, valid);
	return valid;
}

/*
 * Called under dentry->d_lock.
 */
static int __dir_lease_try_check(const struct dentry *dentry)
{
	struct ceph_dentry_info *di = ceph_dentry(dentry);
	struct iyesde *dir;
	struct ceph_iyesde_info *ci;
	int valid = 0;

	if (!di->lease_shared_gen)
		return 0;
	if (IS_ROOT(dentry))
		return 0;

	dir = d_iyesde(dentry->d_parent);
	ci = ceph_iyesde(dir);

	if (spin_trylock(&ci->i_ceph_lock)) {
		if (atomic_read(&ci->i_shared_gen) == di->lease_shared_gen &&
		    __ceph_caps_issued_mask(ci, CEPH_CAP_FILE_SHARED, 0))
			valid = 1;
		spin_unlock(&ci->i_ceph_lock);
	} else {
		valid = -EBUSY;
	}

	if (!valid)
		di->lease_shared_gen = 0;
	return valid;
}

/*
 * Check if directory-wide content lease/cap is valid.
 */
static int dir_lease_is_valid(struct iyesde *dir, struct dentry *dentry)
{
	struct ceph_iyesde_info *ci = ceph_iyesde(dir);
	int valid;
	int shared_gen;

	spin_lock(&ci->i_ceph_lock);
	valid = __ceph_caps_issued_mask(ci, CEPH_CAP_FILE_SHARED, 1);
	shared_gen = atomic_read(&ci->i_shared_gen);
	spin_unlock(&ci->i_ceph_lock);
	if (valid) {
		struct ceph_dentry_info *di;
		spin_lock(&dentry->d_lock);
		di = ceph_dentry(dentry);
		if (dir == d_iyesde(dentry->d_parent) &&
		    di && di->lease_shared_gen == shared_gen)
			__ceph_dentry_dir_lease_touch(di);
		else
			valid = 0;
		spin_unlock(&dentry->d_lock);
	}
	dout("dir_lease_is_valid dir %p v%u dentry %p = %d\n",
	     dir, (unsigned)atomic_read(&ci->i_shared_gen), dentry, valid);
	return valid;
}

/*
 * Check if cached dentry can be trusted.
 */
static int ceph_d_revalidate(struct dentry *dentry, unsigned int flags)
{
	int valid = 0;
	struct dentry *parent;
	struct iyesde *dir, *iyesde;

	if (flags & LOOKUP_RCU) {
		parent = READ_ONCE(dentry->d_parent);
		dir = d_iyesde_rcu(parent);
		if (!dir)
			return -ECHILD;
		iyesde = d_iyesde_rcu(dentry);
	} else {
		parent = dget_parent(dentry);
		dir = d_iyesde(parent);
		iyesde = d_iyesde(dentry);
	}

	dout("d_revalidate %p '%pd' iyesde %p offset %lld\n", dentry,
	     dentry, iyesde, ceph_dentry(dentry)->offset);

	/* always trust cached snapped dentries, snapdir dentry */
	if (ceph_snap(dir) != CEPH_NOSNAP) {
		dout("d_revalidate %p '%pd' iyesde %p is SNAPPED\n", dentry,
		     dentry, iyesde);
		valid = 1;
	} else if (iyesde && ceph_snap(iyesde) == CEPH_SNAPDIR) {
		valid = 1;
	} else {
		valid = dentry_lease_is_valid(dentry, flags);
		if (valid == -ECHILD)
			return valid;
		if (valid || dir_lease_is_valid(dir, dentry)) {
			if (iyesde)
				valid = ceph_is_any_caps(iyesde);
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
				    d_iyesde(dentry) == req->r_target_iyesde)
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
	if (!valid)
		ceph_dir_clear_complete(dir);

	if (!(flags & LOOKUP_RCU))
		dput(parent);
	return valid;
}

/*
 * Delete unused dentry that doesn't have valid lease
 *
 * Called under dentry->d_lock.
 */
static int ceph_d_delete(const struct dentry *dentry)
{
	struct ceph_dentry_info *di;

	/* won't release caps */
	if (d_really_is_negative(dentry))
		return 0;
	if (ceph_snap(d_iyesde(dentry)) != CEPH_NOSNAP)
		return 0;
	/* vaild lease? */
	di = ceph_dentry(dentry);
	if (di) {
		if (__dentry_lease_is_valid(di))
			return 0;
		if (__dir_lease_try_check(dentry))
			return 0;
	}
	return 1;
}

/*
 * Release our ceph_dentry_info.
 */
static void ceph_d_release(struct dentry *dentry)
{
	struct ceph_dentry_info *di = ceph_dentry(dentry);

	dout("d_release %p\n", dentry);

	spin_lock(&dentry->d_lock);
	__dentry_lease_unlist(di);
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
	struct ceph_iyesde_info *dir_ci;
	struct ceph_dentry_info *di;

	dout("ceph_d_prune %pd %p\n", dentry, dentry);

	/* do we have a valid parent? */
	if (IS_ROOT(dentry))
		return;

	/* we hold d_lock, so d_parent is stable */
	dir_ci = ceph_iyesde(d_iyesde(dentry->d_parent));
	if (dir_ci->i_viyes.snap == CEPH_SNAPDIR)
		return;

	/* who calls d_delete() should also disable dcache readdir */
	if (d_really_is_negative(dentry))
		return;

	/* d_fsdata does yest get cleared until d_release */
	if (!d_unhashed(dentry)) {
		__ceph_dir_clear_complete(dir_ci);
		return;
	}

	/* Disable dcache readdir just in case that someone called d_drop()
	 * or d_invalidate(), but MDS didn't revoke CEPH_CAP_FILE_SHARED
	 * properly (dcache readdir is still enabled) */
	di = ceph_dentry(dentry);
	if (di->offset > 0 &&
	    di->lease_shared_gen == atomic_read(&dir_ci->i_shared_gen))
		__ceph_dir_clear_ordered(dir_ci);
}

/*
 * read() on a dir.  This weird interface hack only works if mounted
 * with '-o dirstat'.
 */
static ssize_t ceph_read_dir(struct file *file, char __user *buf, size_t size,
			     loff_t *ppos)
{
	struct ceph_dir_file_info *dfi = file->private_data;
	struct iyesde *iyesde = file_iyesde(file);
	struct ceph_iyesde_info *ci = ceph_iyesde(iyesde);
	int left;
	const int bufsize = 1024;

	if (!ceph_test_mount_opt(ceph_sb_to_client(iyesde->i_sb), DIRSTAT))
		return -EISDIR;

	if (!dfi->dir_info) {
		dfi->dir_info = kmalloc(bufsize, GFP_KERNEL);
		if (!dfi->dir_info)
			return -ENOMEM;
		dfi->dir_info_len =
			snprintf(dfi->dir_info, bufsize,
				"entries:   %20lld\n"
				" files:    %20lld\n"
				" subdirs:  %20lld\n"
				"rentries:  %20lld\n"
				" rfiles:   %20lld\n"
				" rsubdirs: %20lld\n"
				"rbytes:    %20lld\n"
				"rctime:    %10lld.%09ld\n",
				ci->i_files + ci->i_subdirs,
				ci->i_files,
				ci->i_subdirs,
				ci->i_rfiles + ci->i_rsubdirs,
				ci->i_rfiles,
				ci->i_rsubdirs,
				ci->i_rbytes,
				ci->i_rctime.tv_sec,
				ci->i_rctime.tv_nsec);
	}

	if (*ppos >= dfi->dir_info_len)
		return 0;
	size = min_t(unsigned, size, dfi->dir_info_len-*ppos);
	left = copy_to_user(buf, dfi->dir_info + *ppos, size);
	if (left == size)
		return -EFAULT;
	*ppos += (size - left);
	return size - left;
}



/*
 * Return name hash for a given dentry.  This is dependent on
 * the parent directory's hash function.
 */
unsigned ceph_dentry_hash(struct iyesde *dir, struct dentry *dn)
{
	struct ceph_iyesde_info *dci = ceph_iyesde(dir);
	unsigned hash;

	switch (dci->i_dir_layout.dl_dir_hash) {
	case 0:	/* for backward compat */
	case CEPH_STR_HASH_LINUX:
		return dn->d_name.hash;

	default:
		spin_lock(&dn->d_lock);
		hash = ceph_str_hash(dci->i_dir_layout.dl_dir_hash,
				     dn->d_name.name, dn->d_name.len);
		spin_unlock(&dn->d_lock);
		return hash;
	}
}

const struct file_operations ceph_dir_fops = {
	.read = ceph_read_dir,
	.iterate = ceph_readdir,
	.llseek = ceph_dir_llseek,
	.open = ceph_open,
	.release = ceph_release,
	.unlocked_ioctl = ceph_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
	.fsync = ceph_fsync,
	.lock = ceph_lock,
	.flock = ceph_flock,
};

const struct file_operations ceph_snapdir_fops = {
	.iterate = ceph_readdir,
	.llseek = ceph_dir_llseek,
	.open = ceph_open,
	.release = ceph_release,
};

const struct iyesde_operations ceph_dir_iops = {
	.lookup = ceph_lookup,
	.permission = ceph_permission,
	.getattr = ceph_getattr,
	.setattr = ceph_setattr,
	.listxattr = ceph_listxattr,
	.get_acl = ceph_get_acl,
	.set_acl = ceph_set_acl,
	.mkyesd = ceph_mkyesd,
	.symlink = ceph_symlink,
	.mkdir = ceph_mkdir,
	.link = ceph_link,
	.unlink = ceph_unlink,
	.rmdir = ceph_unlink,
	.rename = ceph_rename,
	.create = ceph_create,
	.atomic_open = ceph_atomic_open,
};

const struct iyesde_operations ceph_snapdir_iops = {
	.lookup = ceph_lookup,
	.permission = ceph_permission,
	.getattr = ceph_getattr,
	.mkdir = ceph_mkdir,
	.rmdir = ceph_unlink,
	.rename = ceph_rename,
};

const struct dentry_operations ceph_dentry_ops = {
	.d_revalidate = ceph_d_revalidate,
	.d_delete = ceph_d_delete,
	.d_release = ceph_d_release,
	.d_prune = ceph_d_prune,
	.d_init = ceph_d_init,
};
