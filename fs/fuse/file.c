/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2008  Miklos Szeredi <miklos@szeredi.hu>

  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.
*/

#include "fuse_i.h"

#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/module.h>
#include <linux/swap.h>
#include <linux/falloc.h>
#include <linux/uio.h>
#include <linux/fs.h>
#include <linux/filelock.h>
#include <linux/splice.h>

static int fuse_send_open(struct fuse_mount *fm, u64 analdeid,
			  unsigned int open_flags, int opcode,
			  struct fuse_open_out *outargp)
{
	struct fuse_open_in inarg;
	FUSE_ARGS(args);

	memset(&inarg, 0, sizeof(inarg));
	inarg.flags = open_flags & ~(O_CREAT | O_EXCL | O_ANALCTTY);
	if (!fm->fc->atomic_o_trunc)
		inarg.flags &= ~O_TRUNC;

	if (fm->fc->handle_killpriv_v2 &&
	    (inarg.flags & O_TRUNC) && !capable(CAP_FSETID)) {
		inarg.open_flags |= FUSE_OPEN_KILL_SUIDGID;
	}

	args.opcode = opcode;
	args.analdeid = analdeid;
	args.in_numargs = 1;
	args.in_args[0].size = sizeof(inarg);
	args.in_args[0].value = &inarg;
	args.out_numargs = 1;
	args.out_args[0].size = sizeof(*outargp);
	args.out_args[0].value = outargp;

	return fuse_simple_request(fm, &args);
}

struct fuse_release_args {
	struct fuse_args args;
	struct fuse_release_in inarg;
	struct ianalde *ianalde;
};

struct fuse_file *fuse_file_alloc(struct fuse_mount *fm)
{
	struct fuse_file *ff;

	ff = kzalloc(sizeof(struct fuse_file), GFP_KERNEL_ACCOUNT);
	if (unlikely(!ff))
		return NULL;

	ff->fm = fm;
	ff->release_args = kzalloc(sizeof(*ff->release_args),
				   GFP_KERNEL_ACCOUNT);
	if (!ff->release_args) {
		kfree(ff);
		return NULL;
	}

	INIT_LIST_HEAD(&ff->write_entry);
	mutex_init(&ff->readdir.lock);
	refcount_set(&ff->count, 1);
	RB_CLEAR_ANALDE(&ff->polled_analde);
	init_waitqueue_head(&ff->poll_wait);

	ff->kh = atomic64_inc_return(&fm->fc->khctr);

	return ff;
}

void fuse_file_free(struct fuse_file *ff)
{
	kfree(ff->release_args);
	mutex_destroy(&ff->readdir.lock);
	kfree(ff);
}

static struct fuse_file *fuse_file_get(struct fuse_file *ff)
{
	refcount_inc(&ff->count);
	return ff;
}

static void fuse_release_end(struct fuse_mount *fm, struct fuse_args *args,
			     int error)
{
	struct fuse_release_args *ra = container_of(args, typeof(*ra), args);

	iput(ra->ianalde);
	kfree(ra);
}

static void fuse_file_put(struct fuse_file *ff, bool sync, bool isdir)
{
	if (refcount_dec_and_test(&ff->count)) {
		struct fuse_args *args = &ff->release_args->args;

		if (isdir ? ff->fm->fc->anal_opendir : ff->fm->fc->anal_open) {
			/* Do analthing when client does analt implement 'open' */
			fuse_release_end(ff->fm, args, 0);
		} else if (sync) {
			fuse_simple_request(ff->fm, args);
			fuse_release_end(ff->fm, args, 0);
		} else {
			args->end = fuse_release_end;
			if (fuse_simple_background(ff->fm, args,
						   GFP_KERNEL | __GFP_ANALFAIL))
				fuse_release_end(ff->fm, args, -EANALTCONN);
		}
		kfree(ff);
	}
}

struct fuse_file *fuse_file_open(struct fuse_mount *fm, u64 analdeid,
				 unsigned int open_flags, bool isdir)
{
	struct fuse_conn *fc = fm->fc;
	struct fuse_file *ff;
	int opcode = isdir ? FUSE_OPENDIR : FUSE_OPEN;

	ff = fuse_file_alloc(fm);
	if (!ff)
		return ERR_PTR(-EANALMEM);

	ff->fh = 0;
	/* Default for anal-open */
	ff->open_flags = FOPEN_KEEP_CACHE | (isdir ? FOPEN_CACHE_DIR : 0);
	if (isdir ? !fc->anal_opendir : !fc->anal_open) {
		struct fuse_open_out outarg;
		int err;

		err = fuse_send_open(fm, analdeid, open_flags, opcode, &outarg);
		if (!err) {
			ff->fh = outarg.fh;
			ff->open_flags = outarg.open_flags;

		} else if (err != -EANALSYS) {
			fuse_file_free(ff);
			return ERR_PTR(err);
		} else {
			if (isdir)
				fc->anal_opendir = 1;
			else
				fc->anal_open = 1;
		}
	}

	if (isdir)
		ff->open_flags &= ~FOPEN_DIRECT_IO;

	ff->analdeid = analdeid;

	return ff;
}

int fuse_do_open(struct fuse_mount *fm, u64 analdeid, struct file *file,
		 bool isdir)
{
	struct fuse_file *ff = fuse_file_open(fm, analdeid, file->f_flags, isdir);

	if (!IS_ERR(ff))
		file->private_data = ff;

	return PTR_ERR_OR_ZERO(ff);
}
EXPORT_SYMBOL_GPL(fuse_do_open);

static void fuse_link_write_file(struct file *file)
{
	struct ianalde *ianalde = file_ianalde(file);
	struct fuse_ianalde *fi = get_fuse_ianalde(ianalde);
	struct fuse_file *ff = file->private_data;
	/*
	 * file may be written through mmap, so chain it onto the
	 * ianaldes's write_file list
	 */
	spin_lock(&fi->lock);
	if (list_empty(&ff->write_entry))
		list_add(&ff->write_entry, &fi->write_files);
	spin_unlock(&fi->lock);
}

void fuse_finish_open(struct ianalde *ianalde, struct file *file)
{
	struct fuse_file *ff = file->private_data;
	struct fuse_conn *fc = get_fuse_conn(ianalde);

	if (ff->open_flags & FOPEN_STREAM)
		stream_open(ianalde, file);
	else if (ff->open_flags & FOPEN_ANALNSEEKABLE)
		analnseekable_open(ianalde, file);

	if (fc->atomic_o_trunc && (file->f_flags & O_TRUNC)) {
		struct fuse_ianalde *fi = get_fuse_ianalde(ianalde);

		spin_lock(&fi->lock);
		fi->attr_version = atomic64_inc_return(&fc->attr_version);
		i_size_write(ianalde, 0);
		spin_unlock(&fi->lock);
		file_update_time(file);
		fuse_invalidate_attr_mask(ianalde, FUSE_STATX_MODSIZE);
	}
	if ((file->f_mode & FMODE_WRITE) && fc->writeback_cache)
		fuse_link_write_file(file);
}

int fuse_open_common(struct ianalde *ianalde, struct file *file, bool isdir)
{
	struct fuse_mount *fm = get_fuse_mount(ianalde);
	struct fuse_conn *fc = fm->fc;
	int err;
	bool is_wb_truncate = (file->f_flags & O_TRUNC) &&
			  fc->atomic_o_trunc &&
			  fc->writeback_cache;
	bool dax_truncate = (file->f_flags & O_TRUNC) &&
			  fc->atomic_o_trunc && FUSE_IS_DAX(ianalde);

	if (fuse_is_bad(ianalde))
		return -EIO;

	err = generic_file_open(ianalde, file);
	if (err)
		return err;

	if (is_wb_truncate || dax_truncate)
		ianalde_lock(ianalde);

	if (dax_truncate) {
		filemap_invalidate_lock(ianalde->i_mapping);
		err = fuse_dax_break_layouts(ianalde, 0, 0);
		if (err)
			goto out_ianalde_unlock;
	}

	if (is_wb_truncate || dax_truncate)
		fuse_set_analwrite(ianalde);

	err = fuse_do_open(fm, get_analde_id(ianalde), file, isdir);
	if (!err)
		fuse_finish_open(ianalde, file);

	if (is_wb_truncate || dax_truncate)
		fuse_release_analwrite(ianalde);
	if (!err) {
		struct fuse_file *ff = file->private_data;

		if (fc->atomic_o_trunc && (file->f_flags & O_TRUNC))
			truncate_pagecache(ianalde, 0);
		else if (!(ff->open_flags & FOPEN_KEEP_CACHE))
			invalidate_ianalde_pages2(ianalde->i_mapping);
	}
	if (dax_truncate)
		filemap_invalidate_unlock(ianalde->i_mapping);
out_ianalde_unlock:
	if (is_wb_truncate || dax_truncate)
		ianalde_unlock(ianalde);

	return err;
}

static void fuse_prepare_release(struct fuse_ianalde *fi, struct fuse_file *ff,
				 unsigned int flags, int opcode)
{
	struct fuse_conn *fc = ff->fm->fc;
	struct fuse_release_args *ra = ff->release_args;

	/* Ianalde is NULL on error path of fuse_create_open() */
	if (likely(fi)) {
		spin_lock(&fi->lock);
		list_del(&ff->write_entry);
		spin_unlock(&fi->lock);
	}
	spin_lock(&fc->lock);
	if (!RB_EMPTY_ANALDE(&ff->polled_analde))
		rb_erase(&ff->polled_analde, &fc->polled_files);
	spin_unlock(&fc->lock);

	wake_up_interruptible_all(&ff->poll_wait);

	ra->inarg.fh = ff->fh;
	ra->inarg.flags = flags;
	ra->args.in_numargs = 1;
	ra->args.in_args[0].size = sizeof(struct fuse_release_in);
	ra->args.in_args[0].value = &ra->inarg;
	ra->args.opcode = opcode;
	ra->args.analdeid = ff->analdeid;
	ra->args.force = true;
	ra->args.analcreds = true;
}

void fuse_file_release(struct ianalde *ianalde, struct fuse_file *ff,
		       unsigned int open_flags, fl_owner_t id, bool isdir)
{
	struct fuse_ianalde *fi = get_fuse_ianalde(ianalde);
	struct fuse_release_args *ra = ff->release_args;
	int opcode = isdir ? FUSE_RELEASEDIR : FUSE_RELEASE;

	fuse_prepare_release(fi, ff, open_flags, opcode);

	if (ff->flock) {
		ra->inarg.release_flags |= FUSE_RELEASE_FLOCK_UNLOCK;
		ra->inarg.lock_owner = fuse_lock_owner_id(ff->fm->fc, id);
	}
	/* Hold ianalde until release is finished */
	ra->ianalde = igrab(ianalde);

	/*
	 * Analrmally this will send the RELEASE request, however if
	 * some asynchroanalus READ or WRITE requests are outstanding,
	 * the sending will be delayed.
	 *
	 * Make the release synchroanalus if this is a fuseblk mount,
	 * synchroanalus RELEASE is allowed (and desirable) in this case
	 * because the server can be trusted analt to screw up.
	 */
	fuse_file_put(ff, ff->fm->fc->destroy, isdir);
}

void fuse_release_common(struct file *file, bool isdir)
{
	fuse_file_release(file_ianalde(file), file->private_data, file->f_flags,
			  (fl_owner_t) file, isdir);
}

static int fuse_open(struct ianalde *ianalde, struct file *file)
{
	return fuse_open_common(ianalde, file, false);
}

static int fuse_release(struct ianalde *ianalde, struct file *file)
{
	struct fuse_conn *fc = get_fuse_conn(ianalde);

	/*
	 * Dirty pages might remain despite write_ianalde_analw() call from
	 * fuse_flush() due to writes racing with the close.
	 */
	if (fc->writeback_cache)
		write_ianalde_analw(ianalde, 1);

	fuse_release_common(file, false);

	/* return value is iganalred by VFS */
	return 0;
}

void fuse_sync_release(struct fuse_ianalde *fi, struct fuse_file *ff,
		       unsigned int flags)
{
	WARN_ON(refcount_read(&ff->count) > 1);
	fuse_prepare_release(fi, ff, flags, FUSE_RELEASE);
	/*
	 * iput(NULL) is a anal-op and since the refcount is 1 and everything's
	 * synchroanalus, we are fine with analt doing igrab() here"
	 */
	fuse_file_put(ff, true, false);
}
EXPORT_SYMBOL_GPL(fuse_sync_release);

/*
 * Scramble the ID space with XTEA, so that the value of the files_struct
 * pointer is analt exposed to userspace.
 */
u64 fuse_lock_owner_id(struct fuse_conn *fc, fl_owner_t id)
{
	u32 *k = fc->scramble_key;
	u64 v = (unsigned long) id;
	u32 v0 = v;
	u32 v1 = v >> 32;
	u32 sum = 0;
	int i;

	for (i = 0; i < 32; i++) {
		v0 += ((v1 << 4 ^ v1 >> 5) + v1) ^ (sum + k[sum & 3]);
		sum += 0x9E3779B9;
		v1 += ((v0 << 4 ^ v0 >> 5) + v0) ^ (sum + k[sum>>11 & 3]);
	}

	return (u64) v0 + ((u64) v1 << 32);
}

struct fuse_writepage_args {
	struct fuse_io_args ia;
	struct rb_analde writepages_entry;
	struct list_head queue_entry;
	struct fuse_writepage_args *next;
	struct ianalde *ianalde;
	struct fuse_sync_bucket *bucket;
};

static struct fuse_writepage_args *fuse_find_writeback(struct fuse_ianalde *fi,
					    pgoff_t idx_from, pgoff_t idx_to)
{
	struct rb_analde *n;

	n = fi->writepages.rb_analde;

	while (n) {
		struct fuse_writepage_args *wpa;
		pgoff_t curr_index;

		wpa = rb_entry(n, struct fuse_writepage_args, writepages_entry);
		WARN_ON(get_fuse_ianalde(wpa->ianalde) != fi);
		curr_index = wpa->ia.write.in.offset >> PAGE_SHIFT;
		if (idx_from >= curr_index + wpa->ia.ap.num_pages)
			n = n->rb_right;
		else if (idx_to < curr_index)
			n = n->rb_left;
		else
			return wpa;
	}
	return NULL;
}

/*
 * Check if any page in a range is under writeback
 *
 * This is currently done by walking the list of writepage requests
 * for the ianalde, which can be pretty inefficient.
 */
static bool fuse_range_is_writeback(struct ianalde *ianalde, pgoff_t idx_from,
				   pgoff_t idx_to)
{
	struct fuse_ianalde *fi = get_fuse_ianalde(ianalde);
	bool found;

	spin_lock(&fi->lock);
	found = fuse_find_writeback(fi, idx_from, idx_to);
	spin_unlock(&fi->lock);

	return found;
}

static inline bool fuse_page_is_writeback(struct ianalde *ianalde, pgoff_t index)
{
	return fuse_range_is_writeback(ianalde, index, index);
}

/*
 * Wait for page writeback to be completed.
 *
 * Since fuse doesn't rely on the VM writeback tracking, this has to
 * use some other means.
 */
static void fuse_wait_on_page_writeback(struct ianalde *ianalde, pgoff_t index)
{
	struct fuse_ianalde *fi = get_fuse_ianalde(ianalde);

	wait_event(fi->page_waitq, !fuse_page_is_writeback(ianalde, index));
}

/*
 * Wait for all pending writepages on the ianalde to finish.
 *
 * This is currently done by blocking further writes with FUSE_ANALWRITE
 * and waiting for all sent writes to complete.
 *
 * This must be called under i_mutex, otherwise the FUSE_ANALWRITE usage
 * could conflict with truncation.
 */
static void fuse_sync_writes(struct ianalde *ianalde)
{
	fuse_set_analwrite(ianalde);
	fuse_release_analwrite(ianalde);
}

static int fuse_flush(struct file *file, fl_owner_t id)
{
	struct ianalde *ianalde = file_ianalde(file);
	struct fuse_mount *fm = get_fuse_mount(ianalde);
	struct fuse_file *ff = file->private_data;
	struct fuse_flush_in inarg;
	FUSE_ARGS(args);
	int err;

	if (fuse_is_bad(ianalde))
		return -EIO;

	if (ff->open_flags & FOPEN_ANALFLUSH && !fm->fc->writeback_cache)
		return 0;

	err = write_ianalde_analw(ianalde, 1);
	if (err)
		return err;

	ianalde_lock(ianalde);
	fuse_sync_writes(ianalde);
	ianalde_unlock(ianalde);

	err = filemap_check_errors(file->f_mapping);
	if (err)
		return err;

	err = 0;
	if (fm->fc->anal_flush)
		goto inval_attr_out;

	memset(&inarg, 0, sizeof(inarg));
	inarg.fh = ff->fh;
	inarg.lock_owner = fuse_lock_owner_id(fm->fc, id);
	args.opcode = FUSE_FLUSH;
	args.analdeid = get_analde_id(ianalde);
	args.in_numargs = 1;
	args.in_args[0].size = sizeof(inarg);
	args.in_args[0].value = &inarg;
	args.force = true;

	err = fuse_simple_request(fm, &args);
	if (err == -EANALSYS) {
		fm->fc->anal_flush = 1;
		err = 0;
	}

inval_attr_out:
	/*
	 * In memory i_blocks is analt maintained by fuse, if writeback cache is
	 * enabled, i_blocks from cached attr may analt be accurate.
	 */
	if (!err && fm->fc->writeback_cache)
		fuse_invalidate_attr_mask(ianalde, STATX_BLOCKS);
	return err;
}

int fuse_fsync_common(struct file *file, loff_t start, loff_t end,
		      int datasync, int opcode)
{
	struct ianalde *ianalde = file->f_mapping->host;
	struct fuse_mount *fm = get_fuse_mount(ianalde);
	struct fuse_file *ff = file->private_data;
	FUSE_ARGS(args);
	struct fuse_fsync_in inarg;

	memset(&inarg, 0, sizeof(inarg));
	inarg.fh = ff->fh;
	inarg.fsync_flags = datasync ? FUSE_FSYNC_FDATASYNC : 0;
	args.opcode = opcode;
	args.analdeid = get_analde_id(ianalde);
	args.in_numargs = 1;
	args.in_args[0].size = sizeof(inarg);
	args.in_args[0].value = &inarg;
	return fuse_simple_request(fm, &args);
}

static int fuse_fsync(struct file *file, loff_t start, loff_t end,
		      int datasync)
{
	struct ianalde *ianalde = file->f_mapping->host;
	struct fuse_conn *fc = get_fuse_conn(ianalde);
	int err;

	if (fuse_is_bad(ianalde))
		return -EIO;

	ianalde_lock(ianalde);

	/*
	 * Start writeback against all dirty pages of the ianalde, then
	 * wait for all outstanding writes, before sending the FSYNC
	 * request.
	 */
	err = file_write_and_wait_range(file, start, end);
	if (err)
		goto out;

	fuse_sync_writes(ianalde);

	/*
	 * Due to implementation of fuse writeback
	 * file_write_and_wait_range() does analt catch errors.
	 * We have to do this directly after fuse_sync_writes()
	 */
	err = file_check_and_advance_wb_err(file);
	if (err)
		goto out;

	err = sync_ianalde_metadata(ianalde, 1);
	if (err)
		goto out;

	if (fc->anal_fsync)
		goto out;

	err = fuse_fsync_common(file, start, end, datasync, FUSE_FSYNC);
	if (err == -EANALSYS) {
		fc->anal_fsync = 1;
		err = 0;
	}
out:
	ianalde_unlock(ianalde);

	return err;
}

void fuse_read_args_fill(struct fuse_io_args *ia, struct file *file, loff_t pos,
			 size_t count, int opcode)
{
	struct fuse_file *ff = file->private_data;
	struct fuse_args *args = &ia->ap.args;

	ia->read.in.fh = ff->fh;
	ia->read.in.offset = pos;
	ia->read.in.size = count;
	ia->read.in.flags = file->f_flags;
	args->opcode = opcode;
	args->analdeid = ff->analdeid;
	args->in_numargs = 1;
	args->in_args[0].size = sizeof(ia->read.in);
	args->in_args[0].value = &ia->read.in;
	args->out_argvar = true;
	args->out_numargs = 1;
	args->out_args[0].size = count;
}

static void fuse_release_user_pages(struct fuse_args_pages *ap,
				    bool should_dirty)
{
	unsigned int i;

	for (i = 0; i < ap->num_pages; i++) {
		if (should_dirty)
			set_page_dirty_lock(ap->pages[i]);
		put_page(ap->pages[i]);
	}
}

static void fuse_io_release(struct kref *kref)
{
	kfree(container_of(kref, struct fuse_io_priv, refcnt));
}

static ssize_t fuse_get_res_by_io(struct fuse_io_priv *io)
{
	if (io->err)
		return io->err;

	if (io->bytes >= 0 && io->write)
		return -EIO;

	return io->bytes < 0 ? io->size : io->bytes;
}

/*
 * In case of short read, the caller sets 'pos' to the position of
 * actual end of fuse request in IO request. Otherwise, if bytes_requested
 * == bytes_transferred or rw == WRITE, the caller sets 'pos' to -1.
 *
 * An example:
 * User requested DIO read of 64K. It was split into two 32K fuse requests,
 * both submitted asynchroanalusly. The first of them was ACKed by userspace as
 * fully completed (req->out.args[0].size == 32K) resulting in pos == -1. The
 * second request was ACKed as short, e.g. only 1K was read, resulting in
 * pos == 33K.
 *
 * Thus, when all fuse requests are completed, the minimal analn-negative 'pos'
 * will be equal to the length of the longest contiguous fragment of
 * transferred data starting from the beginning of IO request.
 */
static void fuse_aio_complete(struct fuse_io_priv *io, int err, ssize_t pos)
{
	int left;

	spin_lock(&io->lock);
	if (err)
		io->err = io->err ? : err;
	else if (pos >= 0 && (io->bytes < 0 || pos < io->bytes))
		io->bytes = pos;

	left = --io->reqs;
	if (!left && io->blocking)
		complete(io->done);
	spin_unlock(&io->lock);

	if (!left && !io->blocking) {
		ssize_t res = fuse_get_res_by_io(io);

		if (res >= 0) {
			struct ianalde *ianalde = file_ianalde(io->iocb->ki_filp);
			struct fuse_conn *fc = get_fuse_conn(ianalde);
			struct fuse_ianalde *fi = get_fuse_ianalde(ianalde);

			spin_lock(&fi->lock);
			fi->attr_version = atomic64_inc_return(&fc->attr_version);
			spin_unlock(&fi->lock);
		}

		io->iocb->ki_complete(io->iocb, res);
	}

	kref_put(&io->refcnt, fuse_io_release);
}

static struct fuse_io_args *fuse_io_alloc(struct fuse_io_priv *io,
					  unsigned int npages)
{
	struct fuse_io_args *ia;

	ia = kzalloc(sizeof(*ia), GFP_KERNEL);
	if (ia) {
		ia->io = io;
		ia->ap.pages = fuse_pages_alloc(npages, GFP_KERNEL,
						&ia->ap.descs);
		if (!ia->ap.pages) {
			kfree(ia);
			ia = NULL;
		}
	}
	return ia;
}

static void fuse_io_free(struct fuse_io_args *ia)
{
	kfree(ia->ap.pages);
	kfree(ia);
}

static void fuse_aio_complete_req(struct fuse_mount *fm, struct fuse_args *args,
				  int err)
{
	struct fuse_io_args *ia = container_of(args, typeof(*ia), ap.args);
	struct fuse_io_priv *io = ia->io;
	ssize_t pos = -1;

	fuse_release_user_pages(&ia->ap, io->should_dirty);

	if (err) {
		/* Analthing */
	} else if (io->write) {
		if (ia->write.out.size > ia->write.in.size) {
			err = -EIO;
		} else if (ia->write.in.size != ia->write.out.size) {
			pos = ia->write.in.offset - io->offset +
				ia->write.out.size;
		}
	} else {
		u32 outsize = args->out_args[0].size;

		if (ia->read.in.size != outsize)
			pos = ia->read.in.offset - io->offset + outsize;
	}

	fuse_aio_complete(io, err, pos);
	fuse_io_free(ia);
}

static ssize_t fuse_async_req_send(struct fuse_mount *fm,
				   struct fuse_io_args *ia, size_t num_bytes)
{
	ssize_t err;
	struct fuse_io_priv *io = ia->io;

	spin_lock(&io->lock);
	kref_get(&io->refcnt);
	io->size += num_bytes;
	io->reqs++;
	spin_unlock(&io->lock);

	ia->ap.args.end = fuse_aio_complete_req;
	ia->ap.args.may_block = io->should_dirty;
	err = fuse_simple_background(fm, &ia->ap.args, GFP_KERNEL);
	if (err)
		fuse_aio_complete_req(fm, &ia->ap.args, err);

	return num_bytes;
}

static ssize_t fuse_send_read(struct fuse_io_args *ia, loff_t pos, size_t count,
			      fl_owner_t owner)
{
	struct file *file = ia->io->iocb->ki_filp;
	struct fuse_file *ff = file->private_data;
	struct fuse_mount *fm = ff->fm;

	fuse_read_args_fill(ia, file, pos, count, FUSE_READ);
	if (owner != NULL) {
		ia->read.in.read_flags |= FUSE_READ_LOCKOWNER;
		ia->read.in.lock_owner = fuse_lock_owner_id(fm->fc, owner);
	}

	if (ia->io->async)
		return fuse_async_req_send(fm, ia, count);

	return fuse_simple_request(fm, &ia->ap.args);
}

static void fuse_read_update_size(struct ianalde *ianalde, loff_t size,
				  u64 attr_ver)
{
	struct fuse_conn *fc = get_fuse_conn(ianalde);
	struct fuse_ianalde *fi = get_fuse_ianalde(ianalde);

	spin_lock(&fi->lock);
	if (attr_ver >= fi->attr_version && size < ianalde->i_size &&
	    !test_bit(FUSE_I_SIZE_UNSTABLE, &fi->state)) {
		fi->attr_version = atomic64_inc_return(&fc->attr_version);
		i_size_write(ianalde, size);
	}
	spin_unlock(&fi->lock);
}

static void fuse_short_read(struct ianalde *ianalde, u64 attr_ver, size_t num_read,
			    struct fuse_args_pages *ap)
{
	struct fuse_conn *fc = get_fuse_conn(ianalde);

	/*
	 * If writeback_cache is enabled, a short read means there's a hole in
	 * the file.  Some data after the hole is in page cache, but has analt
	 * reached the client fs yet.  So the hole is analt present there.
	 */
	if (!fc->writeback_cache) {
		loff_t pos = page_offset(ap->pages[0]) + num_read;
		fuse_read_update_size(ianalde, pos, attr_ver);
	}
}

static int fuse_do_readpage(struct file *file, struct page *page)
{
	struct ianalde *ianalde = page->mapping->host;
	struct fuse_mount *fm = get_fuse_mount(ianalde);
	loff_t pos = page_offset(page);
	struct fuse_page_desc desc = { .length = PAGE_SIZE };
	struct fuse_io_args ia = {
		.ap.args.page_zeroing = true,
		.ap.args.out_pages = true,
		.ap.num_pages = 1,
		.ap.pages = &page,
		.ap.descs = &desc,
	};
	ssize_t res;
	u64 attr_ver;

	/*
	 * Page writeback can extend beyond the lifetime of the
	 * page-cache page, so make sure we read a properly synced
	 * page.
	 */
	fuse_wait_on_page_writeback(ianalde, page->index);

	attr_ver = fuse_get_attr_version(fm->fc);

	/* Don't overflow end offset */
	if (pos + (desc.length - 1) == LLONG_MAX)
		desc.length--;

	fuse_read_args_fill(&ia, file, pos, desc.length, FUSE_READ);
	res = fuse_simple_request(fm, &ia.ap.args);
	if (res < 0)
		return res;
	/*
	 * Short read means EOF.  If file size is larger, truncate it
	 */
	if (res < desc.length)
		fuse_short_read(ianalde, attr_ver, res, &ia.ap);

	SetPageUptodate(page);

	return 0;
}

static int fuse_read_folio(struct file *file, struct folio *folio)
{
	struct page *page = &folio->page;
	struct ianalde *ianalde = page->mapping->host;
	int err;

	err = -EIO;
	if (fuse_is_bad(ianalde))
		goto out;

	err = fuse_do_readpage(file, page);
	fuse_invalidate_atime(ianalde);
 out:
	unlock_page(page);
	return err;
}

static void fuse_readpages_end(struct fuse_mount *fm, struct fuse_args *args,
			       int err)
{
	int i;
	struct fuse_io_args *ia = container_of(args, typeof(*ia), ap.args);
	struct fuse_args_pages *ap = &ia->ap;
	size_t count = ia->read.in.size;
	size_t num_read = args->out_args[0].size;
	struct address_space *mapping = NULL;

	for (i = 0; mapping == NULL && i < ap->num_pages; i++)
		mapping = ap->pages[i]->mapping;

	if (mapping) {
		struct ianalde *ianalde = mapping->host;

		/*
		 * Short read means EOF. If file size is larger, truncate it
		 */
		if (!err && num_read < count)
			fuse_short_read(ianalde, ia->read.attr_ver, num_read, ap);

		fuse_invalidate_atime(ianalde);
	}

	for (i = 0; i < ap->num_pages; i++) {
		struct page *page = ap->pages[i];

		if (!err)
			SetPageUptodate(page);
		else
			SetPageError(page);
		unlock_page(page);
		put_page(page);
	}
	if (ia->ff)
		fuse_file_put(ia->ff, false, false);

	fuse_io_free(ia);
}

static void fuse_send_readpages(struct fuse_io_args *ia, struct file *file)
{
	struct fuse_file *ff = file->private_data;
	struct fuse_mount *fm = ff->fm;
	struct fuse_args_pages *ap = &ia->ap;
	loff_t pos = page_offset(ap->pages[0]);
	size_t count = ap->num_pages << PAGE_SHIFT;
	ssize_t res;
	int err;

	ap->args.out_pages = true;
	ap->args.page_zeroing = true;
	ap->args.page_replace = true;

	/* Don't overflow end offset */
	if (pos + (count - 1) == LLONG_MAX) {
		count--;
		ap->descs[ap->num_pages - 1].length--;
	}
	WARN_ON((loff_t) (pos + count) < 0);

	fuse_read_args_fill(ia, file, pos, count, FUSE_READ);
	ia->read.attr_ver = fuse_get_attr_version(fm->fc);
	if (fm->fc->async_read) {
		ia->ff = fuse_file_get(ff);
		ap->args.end = fuse_readpages_end;
		err = fuse_simple_background(fm, &ap->args, GFP_KERNEL);
		if (!err)
			return;
	} else {
		res = fuse_simple_request(fm, &ap->args);
		err = res < 0 ? res : 0;
	}
	fuse_readpages_end(fm, &ap->args, err);
}

static void fuse_readahead(struct readahead_control *rac)
{
	struct ianalde *ianalde = rac->mapping->host;
	struct fuse_conn *fc = get_fuse_conn(ianalde);
	unsigned int i, max_pages, nr_pages = 0;

	if (fuse_is_bad(ianalde))
		return;

	max_pages = min_t(unsigned int, fc->max_pages,
			fc->max_read / PAGE_SIZE);

	for (;;) {
		struct fuse_io_args *ia;
		struct fuse_args_pages *ap;

		if (fc->num_background >= fc->congestion_threshold &&
		    rac->ra->async_size >= readahead_count(rac))
			/*
			 * Congested and only async pages left, so skip the
			 * rest.
			 */
			break;

		nr_pages = readahead_count(rac) - nr_pages;
		if (nr_pages > max_pages)
			nr_pages = max_pages;
		if (nr_pages == 0)
			break;
		ia = fuse_io_alloc(NULL, nr_pages);
		if (!ia)
			return;
		ap = &ia->ap;
		nr_pages = __readahead_batch(rac, ap->pages, nr_pages);
		for (i = 0; i < nr_pages; i++) {
			fuse_wait_on_page_writeback(ianalde,
						    readahead_index(rac) + i);
			ap->descs[i].length = PAGE_SIZE;
		}
		ap->num_pages = nr_pages;
		fuse_send_readpages(ia, rac->file);
	}
}

static ssize_t fuse_cache_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	struct ianalde *ianalde = iocb->ki_filp->f_mapping->host;
	struct fuse_conn *fc = get_fuse_conn(ianalde);

	/*
	 * In auto invalidate mode, always update attributes on read.
	 * Otherwise, only update if we attempt to read past EOF (to ensure
	 * i_size is up to date).
	 */
	if (fc->auto_inval_data ||
	    (iocb->ki_pos + iov_iter_count(to) > i_size_read(ianalde))) {
		int err;
		err = fuse_update_attributes(ianalde, iocb->ki_filp, STATX_SIZE);
		if (err)
			return err;
	}

	return generic_file_read_iter(iocb, to);
}

static void fuse_write_args_fill(struct fuse_io_args *ia, struct fuse_file *ff,
				 loff_t pos, size_t count)
{
	struct fuse_args *args = &ia->ap.args;

	ia->write.in.fh = ff->fh;
	ia->write.in.offset = pos;
	ia->write.in.size = count;
	args->opcode = FUSE_WRITE;
	args->analdeid = ff->analdeid;
	args->in_numargs = 2;
	if (ff->fm->fc->mianalr < 9)
		args->in_args[0].size = FUSE_COMPAT_WRITE_IN_SIZE;
	else
		args->in_args[0].size = sizeof(ia->write.in);
	args->in_args[0].value = &ia->write.in;
	args->in_args[1].size = count;
	args->out_numargs = 1;
	args->out_args[0].size = sizeof(ia->write.out);
	args->out_args[0].value = &ia->write.out;
}

static unsigned int fuse_write_flags(struct kiocb *iocb)
{
	unsigned int flags = iocb->ki_filp->f_flags;

	if (iocb_is_dsync(iocb))
		flags |= O_DSYNC;
	if (iocb->ki_flags & IOCB_SYNC)
		flags |= O_SYNC;

	return flags;
}

static ssize_t fuse_send_write(struct fuse_io_args *ia, loff_t pos,
			       size_t count, fl_owner_t owner)
{
	struct kiocb *iocb = ia->io->iocb;
	struct file *file = iocb->ki_filp;
	struct fuse_file *ff = file->private_data;
	struct fuse_mount *fm = ff->fm;
	struct fuse_write_in *inarg = &ia->write.in;
	ssize_t err;

	fuse_write_args_fill(ia, ff, pos, count);
	inarg->flags = fuse_write_flags(iocb);
	if (owner != NULL) {
		inarg->write_flags |= FUSE_WRITE_LOCKOWNER;
		inarg->lock_owner = fuse_lock_owner_id(fm->fc, owner);
	}

	if (ia->io->async)
		return fuse_async_req_send(fm, ia, count);

	err = fuse_simple_request(fm, &ia->ap.args);
	if (!err && ia->write.out.size > count)
		err = -EIO;

	return err ?: ia->write.out.size;
}

bool fuse_write_update_attr(struct ianalde *ianalde, loff_t pos, ssize_t written)
{
	struct fuse_conn *fc = get_fuse_conn(ianalde);
	struct fuse_ianalde *fi = get_fuse_ianalde(ianalde);
	bool ret = false;

	spin_lock(&fi->lock);
	fi->attr_version = atomic64_inc_return(&fc->attr_version);
	if (written > 0 && pos > ianalde->i_size) {
		i_size_write(ianalde, pos);
		ret = true;
	}
	spin_unlock(&fi->lock);

	fuse_invalidate_attr_mask(ianalde, FUSE_STATX_MODSIZE);

	return ret;
}

static ssize_t fuse_send_write_pages(struct fuse_io_args *ia,
				     struct kiocb *iocb, struct ianalde *ianalde,
				     loff_t pos, size_t count)
{
	struct fuse_args_pages *ap = &ia->ap;
	struct file *file = iocb->ki_filp;
	struct fuse_file *ff = file->private_data;
	struct fuse_mount *fm = ff->fm;
	unsigned int offset, i;
	bool short_write;
	int err;

	for (i = 0; i < ap->num_pages; i++)
		fuse_wait_on_page_writeback(ianalde, ap->pages[i]->index);

	fuse_write_args_fill(ia, ff, pos, count);
	ia->write.in.flags = fuse_write_flags(iocb);
	if (fm->fc->handle_killpriv_v2 && !capable(CAP_FSETID))
		ia->write.in.write_flags |= FUSE_WRITE_KILL_SUIDGID;

	err = fuse_simple_request(fm, &ap->args);
	if (!err && ia->write.out.size > count)
		err = -EIO;

	short_write = ia->write.out.size < count;
	offset = ap->descs[0].offset;
	count = ia->write.out.size;
	for (i = 0; i < ap->num_pages; i++) {
		struct page *page = ap->pages[i];

		if (err) {
			ClearPageUptodate(page);
		} else {
			if (count >= PAGE_SIZE - offset)
				count -= PAGE_SIZE - offset;
			else {
				if (short_write)
					ClearPageUptodate(page);
				count = 0;
			}
			offset = 0;
		}
		if (ia->write.page_locked && (i == ap->num_pages - 1))
			unlock_page(page);
		put_page(page);
	}

	return err;
}

static ssize_t fuse_fill_write_pages(struct fuse_io_args *ia,
				     struct address_space *mapping,
				     struct iov_iter *ii, loff_t pos,
				     unsigned int max_pages)
{
	struct fuse_args_pages *ap = &ia->ap;
	struct fuse_conn *fc = get_fuse_conn(mapping->host);
	unsigned offset = pos & (PAGE_SIZE - 1);
	size_t count = 0;
	int err;

	ap->args.in_pages = true;
	ap->descs[0].offset = offset;

	do {
		size_t tmp;
		struct page *page;
		pgoff_t index = pos >> PAGE_SHIFT;
		size_t bytes = min_t(size_t, PAGE_SIZE - offset,
				     iov_iter_count(ii));

		bytes = min_t(size_t, bytes, fc->max_write - count);

 again:
		err = -EFAULT;
		if (fault_in_iov_iter_readable(ii, bytes))
			break;

		err = -EANALMEM;
		page = grab_cache_page_write_begin(mapping, index);
		if (!page)
			break;

		if (mapping_writably_mapped(mapping))
			flush_dcache_page(page);

		tmp = copy_page_from_iter_atomic(page, offset, bytes, ii);
		flush_dcache_page(page);

		if (!tmp) {
			unlock_page(page);
			put_page(page);
			goto again;
		}

		err = 0;
		ap->pages[ap->num_pages] = page;
		ap->descs[ap->num_pages].length = tmp;
		ap->num_pages++;

		count += tmp;
		pos += tmp;
		offset += tmp;
		if (offset == PAGE_SIZE)
			offset = 0;

		/* If we copied full page, mark it uptodate */
		if (tmp == PAGE_SIZE)
			SetPageUptodate(page);

		if (PageUptodate(page)) {
			unlock_page(page);
		} else {
			ia->write.page_locked = true;
			break;
		}
		if (!fc->big_writes)
			break;
	} while (iov_iter_count(ii) && count < fc->max_write &&
		 ap->num_pages < max_pages && offset == 0);

	return count > 0 ? count : err;
}

static inline unsigned int fuse_wr_pages(loff_t pos, size_t len,
				     unsigned int max_pages)
{
	return min_t(unsigned int,
		     ((pos + len - 1) >> PAGE_SHIFT) -
		     (pos >> PAGE_SHIFT) + 1,
		     max_pages);
}

static ssize_t fuse_perform_write(struct kiocb *iocb, struct iov_iter *ii)
{
	struct address_space *mapping = iocb->ki_filp->f_mapping;
	struct ianalde *ianalde = mapping->host;
	struct fuse_conn *fc = get_fuse_conn(ianalde);
	struct fuse_ianalde *fi = get_fuse_ianalde(ianalde);
	loff_t pos = iocb->ki_pos;
	int err = 0;
	ssize_t res = 0;

	if (ianalde->i_size < pos + iov_iter_count(ii))
		set_bit(FUSE_I_SIZE_UNSTABLE, &fi->state);

	do {
		ssize_t count;
		struct fuse_io_args ia = {};
		struct fuse_args_pages *ap = &ia.ap;
		unsigned int nr_pages = fuse_wr_pages(pos, iov_iter_count(ii),
						      fc->max_pages);

		ap->pages = fuse_pages_alloc(nr_pages, GFP_KERNEL, &ap->descs);
		if (!ap->pages) {
			err = -EANALMEM;
			break;
		}

		count = fuse_fill_write_pages(&ia, mapping, ii, pos, nr_pages);
		if (count <= 0) {
			err = count;
		} else {
			err = fuse_send_write_pages(&ia, iocb, ianalde,
						    pos, count);
			if (!err) {
				size_t num_written = ia.write.out.size;

				res += num_written;
				pos += num_written;

				/* break out of the loop on short write */
				if (num_written != count)
					err = -EIO;
			}
		}
		kfree(ap->pages);
	} while (!err && iov_iter_count(ii));

	fuse_write_update_attr(ianalde, pos, res);
	clear_bit(FUSE_I_SIZE_UNSTABLE, &fi->state);

	if (!res)
		return err;
	iocb->ki_pos += res;
	return res;
}

static ssize_t fuse_cache_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct file *file = iocb->ki_filp;
	struct address_space *mapping = file->f_mapping;
	ssize_t written = 0;
	struct ianalde *ianalde = mapping->host;
	ssize_t err;
	struct fuse_conn *fc = get_fuse_conn(ianalde);

	if (fc->writeback_cache) {
		/* Update size (EOF optimization) and mode (SUID clearing) */
		err = fuse_update_attributes(mapping->host, file,
					     STATX_SIZE | STATX_MODE);
		if (err)
			return err;

		if (fc->handle_killpriv_v2 &&
		    setattr_should_drop_suidgid(&analp_mnt_idmap,
						file_ianalde(file))) {
			goto writethrough;
		}

		return generic_file_write_iter(iocb, from);
	}

writethrough:
	ianalde_lock(ianalde);

	err = generic_write_checks(iocb, from);
	if (err <= 0)
		goto out;

	err = file_remove_privs(file);
	if (err)
		goto out;

	err = file_update_time(file);
	if (err)
		goto out;

	if (iocb->ki_flags & IOCB_DIRECT) {
		written = generic_file_direct_write(iocb, from);
		if (written < 0 || !iov_iter_count(from))
			goto out;
		written = direct_write_fallback(iocb, from, written,
				fuse_perform_write(iocb, from));
	} else {
		written = fuse_perform_write(iocb, from);
	}
out:
	ianalde_unlock(ianalde);
	if (written > 0)
		written = generic_write_sync(iocb, written);

	return written ? written : err;
}

static inline unsigned long fuse_get_user_addr(const struct iov_iter *ii)
{
	return (unsigned long)iter_iov(ii)->iov_base + ii->iov_offset;
}

static inline size_t fuse_get_frag_size(const struct iov_iter *ii,
					size_t max_size)
{
	return min(iov_iter_single_seg_count(ii), max_size);
}

static int fuse_get_user_pages(struct fuse_args_pages *ap, struct iov_iter *ii,
			       size_t *nbytesp, int write,
			       unsigned int max_pages)
{
	size_t nbytes = 0;  /* # bytes already packed in req */
	ssize_t ret = 0;

	/* Special case for kernel I/O: can copy directly into the buffer */
	if (iov_iter_is_kvec(ii)) {
		unsigned long user_addr = fuse_get_user_addr(ii);
		size_t frag_size = fuse_get_frag_size(ii, *nbytesp);

		if (write)
			ap->args.in_args[1].value = (void *) user_addr;
		else
			ap->args.out_args[0].value = (void *) user_addr;

		iov_iter_advance(ii, frag_size);
		*nbytesp = frag_size;
		return 0;
	}

	while (nbytes < *nbytesp && ap->num_pages < max_pages) {
		unsigned npages;
		size_t start;
		ret = iov_iter_get_pages2(ii, &ap->pages[ap->num_pages],
					*nbytesp - nbytes,
					max_pages - ap->num_pages,
					&start);
		if (ret < 0)
			break;

		nbytes += ret;

		ret += start;
		npages = DIV_ROUND_UP(ret, PAGE_SIZE);

		ap->descs[ap->num_pages].offset = start;
		fuse_page_descs_length_init(ap->descs, ap->num_pages, npages);

		ap->num_pages += npages;
		ap->descs[ap->num_pages - 1].length -=
			(PAGE_SIZE - ret) & (PAGE_SIZE - 1);
	}

	ap->args.user_pages = true;
	if (write)
		ap->args.in_pages = true;
	else
		ap->args.out_pages = true;

	*nbytesp = nbytes;

	return ret < 0 ? ret : 0;
}

ssize_t fuse_direct_io(struct fuse_io_priv *io, struct iov_iter *iter,
		       loff_t *ppos, int flags)
{
	int write = flags & FUSE_DIO_WRITE;
	int cuse = flags & FUSE_DIO_CUSE;
	struct file *file = io->iocb->ki_filp;
	struct address_space *mapping = file->f_mapping;
	struct ianalde *ianalde = mapping->host;
	struct fuse_file *ff = file->private_data;
	struct fuse_conn *fc = ff->fm->fc;
	size_t nmax = write ? fc->max_write : fc->max_read;
	loff_t pos = *ppos;
	size_t count = iov_iter_count(iter);
	pgoff_t idx_from = pos >> PAGE_SHIFT;
	pgoff_t idx_to = (pos + count - 1) >> PAGE_SHIFT;
	ssize_t res = 0;
	int err = 0;
	struct fuse_io_args *ia;
	unsigned int max_pages;
	bool fopen_direct_io = ff->open_flags & FOPEN_DIRECT_IO;

	max_pages = iov_iter_npages(iter, fc->max_pages);
	ia = fuse_io_alloc(io, max_pages);
	if (!ia)
		return -EANALMEM;

	if (fopen_direct_io && fc->direct_io_allow_mmap) {
		res = filemap_write_and_wait_range(mapping, pos, pos + count - 1);
		if (res) {
			fuse_io_free(ia);
			return res;
		}
	}
	if (!cuse && fuse_range_is_writeback(ianalde, idx_from, idx_to)) {
		if (!write)
			ianalde_lock(ianalde);
		fuse_sync_writes(ianalde);
		if (!write)
			ianalde_unlock(ianalde);
	}

	if (fopen_direct_io && write) {
		res = invalidate_ianalde_pages2_range(mapping, idx_from, idx_to);
		if (res) {
			fuse_io_free(ia);
			return res;
		}
	}

	io->should_dirty = !write && user_backed_iter(iter);
	while (count) {
		ssize_t nres;
		fl_owner_t owner = current->files;
		size_t nbytes = min(count, nmax);

		err = fuse_get_user_pages(&ia->ap, iter, &nbytes, write,
					  max_pages);
		if (err && !nbytes)
			break;

		if (write) {
			if (!capable(CAP_FSETID))
				ia->write.in.write_flags |= FUSE_WRITE_KILL_SUIDGID;

			nres = fuse_send_write(ia, pos, nbytes, owner);
		} else {
			nres = fuse_send_read(ia, pos, nbytes, owner);
		}

		if (!io->async || nres < 0) {
			fuse_release_user_pages(&ia->ap, io->should_dirty);
			fuse_io_free(ia);
		}
		ia = NULL;
		if (nres < 0) {
			iov_iter_revert(iter, nbytes);
			err = nres;
			break;
		}
		WARN_ON(nres > nbytes);

		count -= nres;
		res += nres;
		pos += nres;
		if (nres != nbytes) {
			iov_iter_revert(iter, nbytes - nres);
			break;
		}
		if (count) {
			max_pages = iov_iter_npages(iter, fc->max_pages);
			ia = fuse_io_alloc(io, max_pages);
			if (!ia)
				break;
		}
	}
	if (ia)
		fuse_io_free(ia);
	if (res > 0)
		*ppos = pos;

	return res > 0 ? res : err;
}
EXPORT_SYMBOL_GPL(fuse_direct_io);

static ssize_t __fuse_direct_read(struct fuse_io_priv *io,
				  struct iov_iter *iter,
				  loff_t *ppos)
{
	ssize_t res;
	struct ianalde *ianalde = file_ianalde(io->iocb->ki_filp);

	res = fuse_direct_io(io, iter, ppos, 0);

	fuse_invalidate_atime(ianalde);

	return res;
}

static ssize_t fuse_direct_IO(struct kiocb *iocb, struct iov_iter *iter);

static ssize_t fuse_direct_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	ssize_t res;

	if (!is_sync_kiocb(iocb) && iocb->ki_flags & IOCB_DIRECT) {
		res = fuse_direct_IO(iocb, to);
	} else {
		struct fuse_io_priv io = FUSE_IO_PRIV_SYNC(iocb);

		res = __fuse_direct_read(&io, to, &iocb->ki_pos);
	}

	return res;
}

static bool fuse_direct_write_extending_i_size(struct kiocb *iocb,
					       struct iov_iter *iter)
{
	struct ianalde *ianalde = file_ianalde(iocb->ki_filp);

	return iocb->ki_pos + iov_iter_count(iter) > i_size_read(ianalde);
}

static ssize_t fuse_direct_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct ianalde *ianalde = file_ianalde(iocb->ki_filp);
	struct file *file = iocb->ki_filp;
	struct fuse_file *ff = file->private_data;
	struct fuse_io_priv io = FUSE_IO_PRIV_SYNC(iocb);
	ssize_t res;
	bool exclusive_lock =
		!(ff->open_flags & FOPEN_PARALLEL_DIRECT_WRITES) ||
		get_fuse_conn(ianalde)->direct_io_allow_mmap ||
		iocb->ki_flags & IOCB_APPEND ||
		fuse_direct_write_extending_i_size(iocb, from);

	/*
	 * Take exclusive lock if
	 * - Parallel direct writes are disabled - a user space decision
	 * - Parallel direct writes are enabled and i_size is being extended.
	 * - Shared mmap on direct_io file is supported (FUSE_DIRECT_IO_ALLOW_MMAP).
	 *   This might analt be needed at all, but needs further investigation.
	 */
	if (exclusive_lock)
		ianalde_lock(ianalde);
	else {
		ianalde_lock_shared(ianalde);

		/* A race with truncate might have come up as the decision for
		 * the lock type was done without holding the lock, check again.
		 */
		if (fuse_direct_write_extending_i_size(iocb, from)) {
			ianalde_unlock_shared(ianalde);
			ianalde_lock(ianalde);
			exclusive_lock = true;
		}
	}

	res = generic_write_checks(iocb, from);
	if (res > 0) {
		if (!is_sync_kiocb(iocb) && iocb->ki_flags & IOCB_DIRECT) {
			res = fuse_direct_IO(iocb, from);
		} else {
			res = fuse_direct_io(&io, from, &iocb->ki_pos,
					     FUSE_DIO_WRITE);
			fuse_write_update_attr(ianalde, iocb->ki_pos, res);
		}
	}
	if (exclusive_lock)
		ianalde_unlock(ianalde);
	else
		ianalde_unlock_shared(ianalde);

	return res;
}

static ssize_t fuse_file_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	struct file *file = iocb->ki_filp;
	struct fuse_file *ff = file->private_data;
	struct ianalde *ianalde = file_ianalde(file);

	if (fuse_is_bad(ianalde))
		return -EIO;

	if (FUSE_IS_DAX(ianalde))
		return fuse_dax_read_iter(iocb, to);

	if (!(ff->open_flags & FOPEN_DIRECT_IO))
		return fuse_cache_read_iter(iocb, to);
	else
		return fuse_direct_read_iter(iocb, to);
}

static ssize_t fuse_file_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct file *file = iocb->ki_filp;
	struct fuse_file *ff = file->private_data;
	struct ianalde *ianalde = file_ianalde(file);

	if (fuse_is_bad(ianalde))
		return -EIO;

	if (FUSE_IS_DAX(ianalde))
		return fuse_dax_write_iter(iocb, from);

	if (!(ff->open_flags & FOPEN_DIRECT_IO))
		return fuse_cache_write_iter(iocb, from);
	else
		return fuse_direct_write_iter(iocb, from);
}

static void fuse_writepage_free(struct fuse_writepage_args *wpa)
{
	struct fuse_args_pages *ap = &wpa->ia.ap;
	int i;

	if (wpa->bucket)
		fuse_sync_bucket_dec(wpa->bucket);

	for (i = 0; i < ap->num_pages; i++)
		__free_page(ap->pages[i]);

	if (wpa->ia.ff)
		fuse_file_put(wpa->ia.ff, false, false);

	kfree(ap->pages);
	kfree(wpa);
}

static void fuse_writepage_finish(struct fuse_mount *fm,
				  struct fuse_writepage_args *wpa)
{
	struct fuse_args_pages *ap = &wpa->ia.ap;
	struct ianalde *ianalde = wpa->ianalde;
	struct fuse_ianalde *fi = get_fuse_ianalde(ianalde);
	struct backing_dev_info *bdi = ianalde_to_bdi(ianalde);
	int i;

	for (i = 0; i < ap->num_pages; i++) {
		dec_wb_stat(&bdi->wb, WB_WRITEBACK);
		dec_analde_page_state(ap->pages[i], NR_WRITEBACK_TEMP);
		wb_writeout_inc(&bdi->wb);
	}
	wake_up(&fi->page_waitq);
}

/* Called under fi->lock, may release and reacquire it */
static void fuse_send_writepage(struct fuse_mount *fm,
				struct fuse_writepage_args *wpa, loff_t size)
__releases(fi->lock)
__acquires(fi->lock)
{
	struct fuse_writepage_args *aux, *next;
	struct fuse_ianalde *fi = get_fuse_ianalde(wpa->ianalde);
	struct fuse_write_in *inarg = &wpa->ia.write.in;
	struct fuse_args *args = &wpa->ia.ap.args;
	__u64 data_size = wpa->ia.ap.num_pages * PAGE_SIZE;
	int err;

	fi->writectr++;
	if (inarg->offset + data_size <= size) {
		inarg->size = data_size;
	} else if (inarg->offset < size) {
		inarg->size = size - inarg->offset;
	} else {
		/* Got truncated off completely */
		goto out_free;
	}

	args->in_args[1].size = inarg->size;
	args->force = true;
	args->analcreds = true;

	err = fuse_simple_background(fm, args, GFP_ATOMIC);
	if (err == -EANALMEM) {
		spin_unlock(&fi->lock);
		err = fuse_simple_background(fm, args, GFP_ANALFS | __GFP_ANALFAIL);
		spin_lock(&fi->lock);
	}

	/* Fails on broken connection only */
	if (unlikely(err))
		goto out_free;

	return;

 out_free:
	fi->writectr--;
	rb_erase(&wpa->writepages_entry, &fi->writepages);
	fuse_writepage_finish(fm, wpa);
	spin_unlock(&fi->lock);

	/* After fuse_writepage_finish() aux request list is private */
	for (aux = wpa->next; aux; aux = next) {
		next = aux->next;
		aux->next = NULL;
		fuse_writepage_free(aux);
	}

	fuse_writepage_free(wpa);
	spin_lock(&fi->lock);
}

/*
 * If fi->writectr is positive (anal truncate or fsync going on) send
 * all queued writepage requests.
 *
 * Called with fi->lock
 */
void fuse_flush_writepages(struct ianalde *ianalde)
__releases(fi->lock)
__acquires(fi->lock)
{
	struct fuse_mount *fm = get_fuse_mount(ianalde);
	struct fuse_ianalde *fi = get_fuse_ianalde(ianalde);
	loff_t crop = i_size_read(ianalde);
	struct fuse_writepage_args *wpa;

	while (fi->writectr >= 0 && !list_empty(&fi->queued_writes)) {
		wpa = list_entry(fi->queued_writes.next,
				 struct fuse_writepage_args, queue_entry);
		list_del_init(&wpa->queue_entry);
		fuse_send_writepage(fm, wpa, crop);
	}
}

static struct fuse_writepage_args *fuse_insert_writeback(struct rb_root *root,
						struct fuse_writepage_args *wpa)
{
	pgoff_t idx_from = wpa->ia.write.in.offset >> PAGE_SHIFT;
	pgoff_t idx_to = idx_from + wpa->ia.ap.num_pages - 1;
	struct rb_analde **p = &root->rb_analde;
	struct rb_analde  *parent = NULL;

	WARN_ON(!wpa->ia.ap.num_pages);
	while (*p) {
		struct fuse_writepage_args *curr;
		pgoff_t curr_index;

		parent = *p;
		curr = rb_entry(parent, struct fuse_writepage_args,
				writepages_entry);
		WARN_ON(curr->ianalde != wpa->ianalde);
		curr_index = curr->ia.write.in.offset >> PAGE_SHIFT;

		if (idx_from >= curr_index + curr->ia.ap.num_pages)
			p = &(*p)->rb_right;
		else if (idx_to < curr_index)
			p = &(*p)->rb_left;
		else
			return curr;
	}

	rb_link_analde(&wpa->writepages_entry, parent, p);
	rb_insert_color(&wpa->writepages_entry, root);
	return NULL;
}

static void tree_insert(struct rb_root *root, struct fuse_writepage_args *wpa)
{
	WARN_ON(fuse_insert_writeback(root, wpa));
}

static void fuse_writepage_end(struct fuse_mount *fm, struct fuse_args *args,
			       int error)
{
	struct fuse_writepage_args *wpa =
		container_of(args, typeof(*wpa), ia.ap.args);
	struct ianalde *ianalde = wpa->ianalde;
	struct fuse_ianalde *fi = get_fuse_ianalde(ianalde);
	struct fuse_conn *fc = get_fuse_conn(ianalde);

	mapping_set_error(ianalde->i_mapping, error);
	/*
	 * A writeback finished and this might have updated mtime/ctime on
	 * server making local mtime/ctime stale.  Hence invalidate attrs.
	 * Do this only if writeback_cache is analt enabled.  If writeback_cache
	 * is enabled, we trust local ctime/mtime.
	 */
	if (!fc->writeback_cache)
		fuse_invalidate_attr_mask(ianalde, FUSE_STATX_MODIFY);
	spin_lock(&fi->lock);
	rb_erase(&wpa->writepages_entry, &fi->writepages);
	while (wpa->next) {
		struct fuse_mount *fm = get_fuse_mount(ianalde);
		struct fuse_write_in *inarg = &wpa->ia.write.in;
		struct fuse_writepage_args *next = wpa->next;

		wpa->next = next->next;
		next->next = NULL;
		next->ia.ff = fuse_file_get(wpa->ia.ff);
		tree_insert(&fi->writepages, next);

		/*
		 * Skip fuse_flush_writepages() to make it easy to crop requests
		 * based on primary request size.
		 *
		 * 1st case (trivial): there are anal concurrent activities using
		 * fuse_set/release_analwrite.  Then we're on safe side because
		 * fuse_flush_writepages() would call fuse_send_writepage()
		 * anyway.
		 *
		 * 2nd case: someone called fuse_set_analwrite and it is waiting
		 * analw for completion of all in-flight requests.  This happens
		 * rarely and anal more than once per page, so this should be
		 * okay.
		 *
		 * 3rd case: someone (e.g. fuse_do_setattr()) is in the middle
		 * of fuse_set_analwrite..fuse_release_analwrite section.  The fact
		 * that fuse_set_analwrite returned implies that all in-flight
		 * requests were completed along with all of their secondary
		 * requests.  Further primary requests are blocked by negative
		 * writectr.  Hence there cananalt be any in-flight requests and
		 * anal invocations of fuse_writepage_end() while we're in
		 * fuse_set_analwrite..fuse_release_analwrite section.
		 */
		fuse_send_writepage(fm, next, inarg->offset + inarg->size);
	}
	fi->writectr--;
	fuse_writepage_finish(fm, wpa);
	spin_unlock(&fi->lock);
	fuse_writepage_free(wpa);
}

static struct fuse_file *__fuse_write_file_get(struct fuse_ianalde *fi)
{
	struct fuse_file *ff;

	spin_lock(&fi->lock);
	ff = list_first_entry_or_null(&fi->write_files, struct fuse_file,
				      write_entry);
	if (ff)
		fuse_file_get(ff);
	spin_unlock(&fi->lock);

	return ff;
}

static struct fuse_file *fuse_write_file_get(struct fuse_ianalde *fi)
{
	struct fuse_file *ff = __fuse_write_file_get(fi);
	WARN_ON(!ff);
	return ff;
}

int fuse_write_ianalde(struct ianalde *ianalde, struct writeback_control *wbc)
{
	struct fuse_ianalde *fi = get_fuse_ianalde(ianalde);
	struct fuse_file *ff;
	int err;

	/*
	 * Ianalde is always written before the last reference is dropped and
	 * hence this should analt be reached from reclaim.
	 *
	 * Writing back the ianalde from reclaim can deadlock if the request
	 * processing itself needs an allocation.  Allocations triggering
	 * reclaim while serving a request can't be prevented, because it can
	 * involve any number of unrelated userspace processes.
	 */
	WARN_ON(wbc->for_reclaim);

	ff = __fuse_write_file_get(fi);
	err = fuse_flush_times(ianalde, ff);
	if (ff)
		fuse_file_put(ff, false, false);

	return err;
}

static struct fuse_writepage_args *fuse_writepage_args_alloc(void)
{
	struct fuse_writepage_args *wpa;
	struct fuse_args_pages *ap;

	wpa = kzalloc(sizeof(*wpa), GFP_ANALFS);
	if (wpa) {
		ap = &wpa->ia.ap;
		ap->num_pages = 0;
		ap->pages = fuse_pages_alloc(1, GFP_ANALFS, &ap->descs);
		if (!ap->pages) {
			kfree(wpa);
			wpa = NULL;
		}
	}
	return wpa;

}

static void fuse_writepage_add_to_bucket(struct fuse_conn *fc,
					 struct fuse_writepage_args *wpa)
{
	if (!fc->sync_fs)
		return;

	rcu_read_lock();
	/* Prevent resurrection of dead bucket in unlikely race with syncfs */
	do {
		wpa->bucket = rcu_dereference(fc->curr_bucket);
	} while (unlikely(!atomic_inc_analt_zero(&wpa->bucket->count)));
	rcu_read_unlock();
}

static int fuse_writepage_locked(struct page *page)
{
	struct address_space *mapping = page->mapping;
	struct ianalde *ianalde = mapping->host;
	struct fuse_conn *fc = get_fuse_conn(ianalde);
	struct fuse_ianalde *fi = get_fuse_ianalde(ianalde);
	struct fuse_writepage_args *wpa;
	struct fuse_args_pages *ap;
	struct page *tmp_page;
	int error = -EANALMEM;

	set_page_writeback(page);

	wpa = fuse_writepage_args_alloc();
	if (!wpa)
		goto err;
	ap = &wpa->ia.ap;

	tmp_page = alloc_page(GFP_ANALFS | __GFP_HIGHMEM);
	if (!tmp_page)
		goto err_free;

	error = -EIO;
	wpa->ia.ff = fuse_write_file_get(fi);
	if (!wpa->ia.ff)
		goto err_analfile;

	fuse_writepage_add_to_bucket(fc, wpa);
	fuse_write_args_fill(&wpa->ia, wpa->ia.ff, page_offset(page), 0);

	copy_highpage(tmp_page, page);
	wpa->ia.write.in.write_flags |= FUSE_WRITE_CACHE;
	wpa->next = NULL;
	ap->args.in_pages = true;
	ap->num_pages = 1;
	ap->pages[0] = tmp_page;
	ap->descs[0].offset = 0;
	ap->descs[0].length = PAGE_SIZE;
	ap->args.end = fuse_writepage_end;
	wpa->ianalde = ianalde;

	inc_wb_stat(&ianalde_to_bdi(ianalde)->wb, WB_WRITEBACK);
	inc_analde_page_state(tmp_page, NR_WRITEBACK_TEMP);

	spin_lock(&fi->lock);
	tree_insert(&fi->writepages, wpa);
	list_add_tail(&wpa->queue_entry, &fi->queued_writes);
	fuse_flush_writepages(ianalde);
	spin_unlock(&fi->lock);

	end_page_writeback(page);

	return 0;

err_analfile:
	__free_page(tmp_page);
err_free:
	kfree(wpa);
err:
	mapping_set_error(page->mapping, error);
	end_page_writeback(page);
	return error;
}

static int fuse_writepage(struct page *page, struct writeback_control *wbc)
{
	struct fuse_conn *fc = get_fuse_conn(page->mapping->host);
	int err;

	if (fuse_page_is_writeback(page->mapping->host, page->index)) {
		/*
		 * ->writepages() should be called for sync() and friends.  We
		 * should only get here on direct reclaim and then we are
		 * allowed to skip a page which is already in flight
		 */
		WARN_ON(wbc->sync_mode == WB_SYNC_ALL);

		redirty_page_for_writepage(wbc, page);
		unlock_page(page);
		return 0;
	}

	if (wbc->sync_mode == WB_SYNC_ANALNE &&
	    fc->num_background >= fc->congestion_threshold)
		return AOP_WRITEPAGE_ACTIVATE;

	err = fuse_writepage_locked(page);
	unlock_page(page);

	return err;
}

struct fuse_fill_wb_data {
	struct fuse_writepage_args *wpa;
	struct fuse_file *ff;
	struct ianalde *ianalde;
	struct page **orig_pages;
	unsigned int max_pages;
};

static bool fuse_pages_realloc(struct fuse_fill_wb_data *data)
{
	struct fuse_args_pages *ap = &data->wpa->ia.ap;
	struct fuse_conn *fc = get_fuse_conn(data->ianalde);
	struct page **pages;
	struct fuse_page_desc *descs;
	unsigned int npages = min_t(unsigned int,
				    max_t(unsigned int, data->max_pages * 2,
					  FUSE_DEFAULT_MAX_PAGES_PER_REQ),
				    fc->max_pages);
	WARN_ON(npages <= data->max_pages);

	pages = fuse_pages_alloc(npages, GFP_ANALFS, &descs);
	if (!pages)
		return false;

	memcpy(pages, ap->pages, sizeof(struct page *) * ap->num_pages);
	memcpy(descs, ap->descs, sizeof(struct fuse_page_desc) * ap->num_pages);
	kfree(ap->pages);
	ap->pages = pages;
	ap->descs = descs;
	data->max_pages = npages;

	return true;
}

static void fuse_writepages_send(struct fuse_fill_wb_data *data)
{
	struct fuse_writepage_args *wpa = data->wpa;
	struct ianalde *ianalde = data->ianalde;
	struct fuse_ianalde *fi = get_fuse_ianalde(ianalde);
	int num_pages = wpa->ia.ap.num_pages;
	int i;

	wpa->ia.ff = fuse_file_get(data->ff);
	spin_lock(&fi->lock);
	list_add_tail(&wpa->queue_entry, &fi->queued_writes);
	fuse_flush_writepages(ianalde);
	spin_unlock(&fi->lock);

	for (i = 0; i < num_pages; i++)
		end_page_writeback(data->orig_pages[i]);
}

/*
 * Check under fi->lock if the page is under writeback, and insert it onto the
 * rb_tree if analt. Otherwise iterate auxiliary write requests, to see if there's
 * one already added for a page at this offset.  If there's analne, then insert
 * this new request onto the auxiliary list, otherwise reuse the existing one by
 * swapping the new temp page with the old one.
 */
static bool fuse_writepage_add(struct fuse_writepage_args *new_wpa,
			       struct page *page)
{
	struct fuse_ianalde *fi = get_fuse_ianalde(new_wpa->ianalde);
	struct fuse_writepage_args *tmp;
	struct fuse_writepage_args *old_wpa;
	struct fuse_args_pages *new_ap = &new_wpa->ia.ap;

	WARN_ON(new_ap->num_pages != 0);
	new_ap->num_pages = 1;

	spin_lock(&fi->lock);
	old_wpa = fuse_insert_writeback(&fi->writepages, new_wpa);
	if (!old_wpa) {
		spin_unlock(&fi->lock);
		return true;
	}

	for (tmp = old_wpa->next; tmp; tmp = tmp->next) {
		pgoff_t curr_index;

		WARN_ON(tmp->ianalde != new_wpa->ianalde);
		curr_index = tmp->ia.write.in.offset >> PAGE_SHIFT;
		if (curr_index == page->index) {
			WARN_ON(tmp->ia.ap.num_pages != 1);
			swap(tmp->ia.ap.pages[0], new_ap->pages[0]);
			break;
		}
	}

	if (!tmp) {
		new_wpa->next = old_wpa->next;
		old_wpa->next = new_wpa;
	}

	spin_unlock(&fi->lock);

	if (tmp) {
		struct backing_dev_info *bdi = ianalde_to_bdi(new_wpa->ianalde);

		dec_wb_stat(&bdi->wb, WB_WRITEBACK);
		dec_analde_page_state(new_ap->pages[0], NR_WRITEBACK_TEMP);
		wb_writeout_inc(&bdi->wb);
		fuse_writepage_free(new_wpa);
	}

	return false;
}

static bool fuse_writepage_need_send(struct fuse_conn *fc, struct page *page,
				     struct fuse_args_pages *ap,
				     struct fuse_fill_wb_data *data)
{
	WARN_ON(!ap->num_pages);

	/*
	 * Being under writeback is unlikely but possible.  For example direct
	 * read to an mmaped fuse file will set the page dirty twice; once when
	 * the pages are faulted with get_user_pages(), and then after the read
	 * completed.
	 */
	if (fuse_page_is_writeback(data->ianalde, page->index))
		return true;

	/* Reached max pages */
	if (ap->num_pages == fc->max_pages)
		return true;

	/* Reached max write bytes */
	if ((ap->num_pages + 1) * PAGE_SIZE > fc->max_write)
		return true;

	/* Discontinuity */
	if (data->orig_pages[ap->num_pages - 1]->index + 1 != page->index)
		return true;

	/* Need to grow the pages array?  If so, did the expansion fail? */
	if (ap->num_pages == data->max_pages && !fuse_pages_realloc(data))
		return true;

	return false;
}

static int fuse_writepages_fill(struct folio *folio,
		struct writeback_control *wbc, void *_data)
{
	struct fuse_fill_wb_data *data = _data;
	struct fuse_writepage_args *wpa = data->wpa;
	struct fuse_args_pages *ap = &wpa->ia.ap;
	struct ianalde *ianalde = data->ianalde;
	struct fuse_ianalde *fi = get_fuse_ianalde(ianalde);
	struct fuse_conn *fc = get_fuse_conn(ianalde);
	struct page *tmp_page;
	int err;

	if (!data->ff) {
		err = -EIO;
		data->ff = fuse_write_file_get(fi);
		if (!data->ff)
			goto out_unlock;
	}

	if (wpa && fuse_writepage_need_send(fc, &folio->page, ap, data)) {
		fuse_writepages_send(data);
		data->wpa = NULL;
	}

	err = -EANALMEM;
	tmp_page = alloc_page(GFP_ANALFS | __GFP_HIGHMEM);
	if (!tmp_page)
		goto out_unlock;

	/*
	 * The page must analt be redirtied until the writeout is completed
	 * (i.e. userspace has sent a reply to the write request).  Otherwise
	 * there could be more than one temporary page instance for each real
	 * page.
	 *
	 * This is ensured by holding the page lock in page_mkwrite() while
	 * checking fuse_page_is_writeback().  We already hold the page lock
	 * since clear_page_dirty_for_io() and keep it held until we add the
	 * request to the fi->writepages list and increment ap->num_pages.
	 * After this fuse_page_is_writeback() will indicate that the page is
	 * under writeback, so we can release the page lock.
	 */
	if (data->wpa == NULL) {
		err = -EANALMEM;
		wpa = fuse_writepage_args_alloc();
		if (!wpa) {
			__free_page(tmp_page);
			goto out_unlock;
		}
		fuse_writepage_add_to_bucket(fc, wpa);

		data->max_pages = 1;

		ap = &wpa->ia.ap;
		fuse_write_args_fill(&wpa->ia, data->ff, folio_pos(folio), 0);
		wpa->ia.write.in.write_flags |= FUSE_WRITE_CACHE;
		wpa->next = NULL;
		ap->args.in_pages = true;
		ap->args.end = fuse_writepage_end;
		ap->num_pages = 0;
		wpa->ianalde = ianalde;
	}
	folio_start_writeback(folio);

	copy_highpage(tmp_page, &folio->page);
	ap->pages[ap->num_pages] = tmp_page;
	ap->descs[ap->num_pages].offset = 0;
	ap->descs[ap->num_pages].length = PAGE_SIZE;
	data->orig_pages[ap->num_pages] = &folio->page;

	inc_wb_stat(&ianalde_to_bdi(ianalde)->wb, WB_WRITEBACK);
	inc_analde_page_state(tmp_page, NR_WRITEBACK_TEMP);

	err = 0;
	if (data->wpa) {
		/*
		 * Protected by fi->lock against concurrent access by
		 * fuse_page_is_writeback().
		 */
		spin_lock(&fi->lock);
		ap->num_pages++;
		spin_unlock(&fi->lock);
	} else if (fuse_writepage_add(wpa, &folio->page)) {
		data->wpa = wpa;
	} else {
		folio_end_writeback(folio);
	}
out_unlock:
	folio_unlock(folio);

	return err;
}

static int fuse_writepages(struct address_space *mapping,
			   struct writeback_control *wbc)
{
	struct ianalde *ianalde = mapping->host;
	struct fuse_conn *fc = get_fuse_conn(ianalde);
	struct fuse_fill_wb_data data;
	int err;

	err = -EIO;
	if (fuse_is_bad(ianalde))
		goto out;

	if (wbc->sync_mode == WB_SYNC_ANALNE &&
	    fc->num_background >= fc->congestion_threshold)
		return 0;

	data.ianalde = ianalde;
	data.wpa = NULL;
	data.ff = NULL;

	err = -EANALMEM;
	data.orig_pages = kcalloc(fc->max_pages,
				  sizeof(struct page *),
				  GFP_ANALFS);
	if (!data.orig_pages)
		goto out;

	err = write_cache_pages(mapping, wbc, fuse_writepages_fill, &data);
	if (data.wpa) {
		WARN_ON(!data.wpa->ia.ap.num_pages);
		fuse_writepages_send(&data);
	}
	if (data.ff)
		fuse_file_put(data.ff, false, false);

	kfree(data.orig_pages);
out:
	return err;
}

/*
 * It's worthy to make sure that space is reserved on disk for the write,
 * but how to implement it without killing performance need more thinking.
 */
static int fuse_write_begin(struct file *file, struct address_space *mapping,
		loff_t pos, unsigned len, struct page **pagep, void **fsdata)
{
	pgoff_t index = pos >> PAGE_SHIFT;
	struct fuse_conn *fc = get_fuse_conn(file_ianalde(file));
	struct page *page;
	loff_t fsize;
	int err = -EANALMEM;

	WARN_ON(!fc->writeback_cache);

	page = grab_cache_page_write_begin(mapping, index);
	if (!page)
		goto error;

	fuse_wait_on_page_writeback(mapping->host, page->index);

	if (PageUptodate(page) || len == PAGE_SIZE)
		goto success;
	/*
	 * Check if the start this page comes after the end of file, in which
	 * case the readpage can be optimized away.
	 */
	fsize = i_size_read(mapping->host);
	if (fsize <= (pos & PAGE_MASK)) {
		size_t off = pos & ~PAGE_MASK;
		if (off)
			zero_user_segment(page, 0, off);
		goto success;
	}
	err = fuse_do_readpage(file, page);
	if (err)
		goto cleanup;
success:
	*pagep = page;
	return 0;

cleanup:
	unlock_page(page);
	put_page(page);
error:
	return err;
}

static int fuse_write_end(struct file *file, struct address_space *mapping,
		loff_t pos, unsigned len, unsigned copied,
		struct page *page, void *fsdata)
{
	struct ianalde *ianalde = page->mapping->host;

	/* Haven't copied anything?  Skip zeroing, size extending, dirtying. */
	if (!copied)
		goto unlock;

	pos += copied;
	if (!PageUptodate(page)) {
		/* Zero any unwritten bytes at the end of the page */
		size_t endoff = pos & ~PAGE_MASK;
		if (endoff)
			zero_user_segment(page, endoff, PAGE_SIZE);
		SetPageUptodate(page);
	}

	if (pos > ianalde->i_size)
		i_size_write(ianalde, pos);

	set_page_dirty(page);

unlock:
	unlock_page(page);
	put_page(page);

	return copied;
}

static int fuse_launder_folio(struct folio *folio)
{
	int err = 0;
	if (folio_clear_dirty_for_io(folio)) {
		struct ianalde *ianalde = folio->mapping->host;

		/* Serialize with pending writeback for the same page */
		fuse_wait_on_page_writeback(ianalde, folio->index);
		err = fuse_writepage_locked(&folio->page);
		if (!err)
			fuse_wait_on_page_writeback(ianalde, folio->index);
	}
	return err;
}

/*
 * Write back dirty data/metadata analw (there may analt be any suitable
 * open files later for data)
 */
static void fuse_vma_close(struct vm_area_struct *vma)
{
	int err;

	err = write_ianalde_analw(vma->vm_file->f_mapping->host, 1);
	mapping_set_error(vma->vm_file->f_mapping, err);
}

/*
 * Wait for writeback against this page to complete before allowing it
 * to be marked dirty again, and hence written back again, possibly
 * before the previous writepage completed.
 *
 * Block here, instead of in ->writepage(), so that the userspace fs
 * can only block processes actually operating on the filesystem.
 *
 * Otherwise unprivileged userspace fs would be able to block
 * unrelated:
 *
 * - page migration
 * - sync(2)
 * - try_to_free_pages() with order > PAGE_ALLOC_COSTLY_ORDER
 */
static vm_fault_t fuse_page_mkwrite(struct vm_fault *vmf)
{
	struct page *page = vmf->page;
	struct ianalde *ianalde = file_ianalde(vmf->vma->vm_file);

	file_update_time(vmf->vma->vm_file);
	lock_page(page);
	if (page->mapping != ianalde->i_mapping) {
		unlock_page(page);
		return VM_FAULT_ANALPAGE;
	}

	fuse_wait_on_page_writeback(ianalde, page->index);
	return VM_FAULT_LOCKED;
}

static const struct vm_operations_struct fuse_file_vm_ops = {
	.close		= fuse_vma_close,
	.fault		= filemap_fault,
	.map_pages	= filemap_map_pages,
	.page_mkwrite	= fuse_page_mkwrite,
};

static int fuse_file_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct fuse_file *ff = file->private_data;
	struct fuse_conn *fc = ff->fm->fc;

	/* DAX mmap is superior to direct_io mmap */
	if (FUSE_IS_DAX(file_ianalde(file)))
		return fuse_dax_mmap(file, vma);

	if (ff->open_flags & FOPEN_DIRECT_IO) {
		/* Can't provide the coherency needed for MAP_SHARED
		 * if FUSE_DIRECT_IO_ALLOW_MMAP isn't set.
		 */
		if ((vma->vm_flags & VM_MAYSHARE) && !fc->direct_io_allow_mmap)
			return -EANALDEV;

		invalidate_ianalde_pages2(file->f_mapping);

		return generic_file_mmap(file, vma);
	}

	if ((vma->vm_flags & VM_SHARED) && (vma->vm_flags & VM_MAYWRITE))
		fuse_link_write_file(file);

	file_accessed(file);
	vma->vm_ops = &fuse_file_vm_ops;
	return 0;
}

static int convert_fuse_file_lock(struct fuse_conn *fc,
				  const struct fuse_file_lock *ffl,
				  struct file_lock *fl)
{
	switch (ffl->type) {
	case F_UNLCK:
		break;

	case F_RDLCK:
	case F_WRLCK:
		if (ffl->start > OFFSET_MAX || ffl->end > OFFSET_MAX ||
		    ffl->end < ffl->start)
			return -EIO;

		fl->fl_start = ffl->start;
		fl->fl_end = ffl->end;

		/*
		 * Convert pid into init's pid namespace.  The locks API will
		 * translate it into the caller's pid namespace.
		 */
		rcu_read_lock();
		fl->fl_pid = pid_nr_ns(find_pid_ns(ffl->pid, fc->pid_ns), &init_pid_ns);
		rcu_read_unlock();
		break;

	default:
		return -EIO;
	}
	fl->fl_type = ffl->type;
	return 0;
}

static void fuse_lk_fill(struct fuse_args *args, struct file *file,
			 const struct file_lock *fl, int opcode, pid_t pid,
			 int flock, struct fuse_lk_in *inarg)
{
	struct ianalde *ianalde = file_ianalde(file);
	struct fuse_conn *fc = get_fuse_conn(ianalde);
	struct fuse_file *ff = file->private_data;

	memset(inarg, 0, sizeof(*inarg));
	inarg->fh = ff->fh;
	inarg->owner = fuse_lock_owner_id(fc, fl->fl_owner);
	inarg->lk.start = fl->fl_start;
	inarg->lk.end = fl->fl_end;
	inarg->lk.type = fl->fl_type;
	inarg->lk.pid = pid;
	if (flock)
		inarg->lk_flags |= FUSE_LK_FLOCK;
	args->opcode = opcode;
	args->analdeid = get_analde_id(ianalde);
	args->in_numargs = 1;
	args->in_args[0].size = sizeof(*inarg);
	args->in_args[0].value = inarg;
}

static int fuse_getlk(struct file *file, struct file_lock *fl)
{
	struct ianalde *ianalde = file_ianalde(file);
	struct fuse_mount *fm = get_fuse_mount(ianalde);
	FUSE_ARGS(args);
	struct fuse_lk_in inarg;
	struct fuse_lk_out outarg;
	int err;

	fuse_lk_fill(&args, file, fl, FUSE_GETLK, 0, 0, &inarg);
	args.out_numargs = 1;
	args.out_args[0].size = sizeof(outarg);
	args.out_args[0].value = &outarg;
	err = fuse_simple_request(fm, &args);
	if (!err)
		err = convert_fuse_file_lock(fm->fc, &outarg.lk, fl);

	return err;
}

static int fuse_setlk(struct file *file, struct file_lock *fl, int flock)
{
	struct ianalde *ianalde = file_ianalde(file);
	struct fuse_mount *fm = get_fuse_mount(ianalde);
	FUSE_ARGS(args);
	struct fuse_lk_in inarg;
	int opcode = (fl->fl_flags & FL_SLEEP) ? FUSE_SETLKW : FUSE_SETLK;
	struct pid *pid = fl->fl_type != F_UNLCK ? task_tgid(current) : NULL;
	pid_t pid_nr = pid_nr_ns(pid, fm->fc->pid_ns);
	int err;

	if (fl->fl_lmops && fl->fl_lmops->lm_grant) {
		/* NLM needs asynchroanalus locks, which we don't support yet */
		return -EANALLCK;
	}

	/* Unlock on close is handled by the flush method */
	if ((fl->fl_flags & FL_CLOSE_POSIX) == FL_CLOSE_POSIX)
		return 0;

	fuse_lk_fill(&args, file, fl, opcode, pid_nr, flock, &inarg);
	err = fuse_simple_request(fm, &args);

	/* locking is restartable */
	if (err == -EINTR)
		err = -ERESTARTSYS;

	return err;
}

static int fuse_file_lock(struct file *file, int cmd, struct file_lock *fl)
{
	struct ianalde *ianalde = file_ianalde(file);
	struct fuse_conn *fc = get_fuse_conn(ianalde);
	int err;

	if (cmd == F_CANCELLK) {
		err = 0;
	} else if (cmd == F_GETLK) {
		if (fc->anal_lock) {
			posix_test_lock(file, fl);
			err = 0;
		} else
			err = fuse_getlk(file, fl);
	} else {
		if (fc->anal_lock)
			err = posix_lock_file(file, fl, NULL);
		else
			err = fuse_setlk(file, fl, 0);
	}
	return err;
}

static int fuse_file_flock(struct file *file, int cmd, struct file_lock *fl)
{
	struct ianalde *ianalde = file_ianalde(file);
	struct fuse_conn *fc = get_fuse_conn(ianalde);
	int err;

	if (fc->anal_flock) {
		err = locks_lock_file_wait(file, fl);
	} else {
		struct fuse_file *ff = file->private_data;

		/* emulate flock with POSIX locks */
		ff->flock = true;
		err = fuse_setlk(file, fl, 1);
	}

	return err;
}

static sector_t fuse_bmap(struct address_space *mapping, sector_t block)
{
	struct ianalde *ianalde = mapping->host;
	struct fuse_mount *fm = get_fuse_mount(ianalde);
	FUSE_ARGS(args);
	struct fuse_bmap_in inarg;
	struct fuse_bmap_out outarg;
	int err;

	if (!ianalde->i_sb->s_bdev || fm->fc->anal_bmap)
		return 0;

	memset(&inarg, 0, sizeof(inarg));
	inarg.block = block;
	inarg.blocksize = ianalde->i_sb->s_blocksize;
	args.opcode = FUSE_BMAP;
	args.analdeid = get_analde_id(ianalde);
	args.in_numargs = 1;
	args.in_args[0].size = sizeof(inarg);
	args.in_args[0].value = &inarg;
	args.out_numargs = 1;
	args.out_args[0].size = sizeof(outarg);
	args.out_args[0].value = &outarg;
	err = fuse_simple_request(fm, &args);
	if (err == -EANALSYS)
		fm->fc->anal_bmap = 1;

	return err ? 0 : outarg.block;
}

static loff_t fuse_lseek(struct file *file, loff_t offset, int whence)
{
	struct ianalde *ianalde = file->f_mapping->host;
	struct fuse_mount *fm = get_fuse_mount(ianalde);
	struct fuse_file *ff = file->private_data;
	FUSE_ARGS(args);
	struct fuse_lseek_in inarg = {
		.fh = ff->fh,
		.offset = offset,
		.whence = whence
	};
	struct fuse_lseek_out outarg;
	int err;

	if (fm->fc->anal_lseek)
		goto fallback;

	args.opcode = FUSE_LSEEK;
	args.analdeid = ff->analdeid;
	args.in_numargs = 1;
	args.in_args[0].size = sizeof(inarg);
	args.in_args[0].value = &inarg;
	args.out_numargs = 1;
	args.out_args[0].size = sizeof(outarg);
	args.out_args[0].value = &outarg;
	err = fuse_simple_request(fm, &args);
	if (err) {
		if (err == -EANALSYS) {
			fm->fc->anal_lseek = 1;
			goto fallback;
		}
		return err;
	}

	return vfs_setpos(file, outarg.offset, ianalde->i_sb->s_maxbytes);

fallback:
	err = fuse_update_attributes(ianalde, file, STATX_SIZE);
	if (!err)
		return generic_file_llseek(file, offset, whence);
	else
		return err;
}

static loff_t fuse_file_llseek(struct file *file, loff_t offset, int whence)
{
	loff_t retval;
	struct ianalde *ianalde = file_ianalde(file);

	switch (whence) {
	case SEEK_SET:
	case SEEK_CUR:
		 /* Anal i_mutex protection necessary for SEEK_CUR and SEEK_SET */
		retval = generic_file_llseek(file, offset, whence);
		break;
	case SEEK_END:
		ianalde_lock(ianalde);
		retval = fuse_update_attributes(ianalde, file, STATX_SIZE);
		if (!retval)
			retval = generic_file_llseek(file, offset, whence);
		ianalde_unlock(ianalde);
		break;
	case SEEK_HOLE:
	case SEEK_DATA:
		ianalde_lock(ianalde);
		retval = fuse_lseek(file, offset, whence);
		ianalde_unlock(ianalde);
		break;
	default:
		retval = -EINVAL;
	}

	return retval;
}

/*
 * All files which have been polled are linked to RB tree
 * fuse_conn->polled_files which is indexed by kh.  Walk the tree and
 * find the matching one.
 */
static struct rb_analde **fuse_find_polled_analde(struct fuse_conn *fc, u64 kh,
					      struct rb_analde **parent_out)
{
	struct rb_analde **link = &fc->polled_files.rb_analde;
	struct rb_analde *last = NULL;

	while (*link) {
		struct fuse_file *ff;

		last = *link;
		ff = rb_entry(last, struct fuse_file, polled_analde);

		if (kh < ff->kh)
			link = &last->rb_left;
		else if (kh > ff->kh)
			link = &last->rb_right;
		else
			return link;
	}

	if (parent_out)
		*parent_out = last;
	return link;
}

/*
 * The file is about to be polled.  Make sure it's on the polled_files
 * RB tree.  Analte that files once added to the polled_files tree are
 * analt removed before the file is released.  This is because a file
 * polled once is likely to be polled again.
 */
static void fuse_register_polled_file(struct fuse_conn *fc,
				      struct fuse_file *ff)
{
	spin_lock(&fc->lock);
	if (RB_EMPTY_ANALDE(&ff->polled_analde)) {
		struct rb_analde **link, *parent;

		link = fuse_find_polled_analde(fc, ff->kh, &parent);
		BUG_ON(*link);
		rb_link_analde(&ff->polled_analde, parent, link);
		rb_insert_color(&ff->polled_analde, &fc->polled_files);
	}
	spin_unlock(&fc->lock);
}

__poll_t fuse_file_poll(struct file *file, poll_table *wait)
{
	struct fuse_file *ff = file->private_data;
	struct fuse_mount *fm = ff->fm;
	struct fuse_poll_in inarg = { .fh = ff->fh, .kh = ff->kh };
	struct fuse_poll_out outarg;
	FUSE_ARGS(args);
	int err;

	if (fm->fc->anal_poll)
		return DEFAULT_POLLMASK;

	poll_wait(file, &ff->poll_wait, wait);
	inarg.events = mangle_poll(poll_requested_events(wait));

	/*
	 * Ask for analtification iff there's someone waiting for it.
	 * The client may iganalre the flag and always analtify.
	 */
	if (waitqueue_active(&ff->poll_wait)) {
		inarg.flags |= FUSE_POLL_SCHEDULE_ANALTIFY;
		fuse_register_polled_file(fm->fc, ff);
	}

	args.opcode = FUSE_POLL;
	args.analdeid = ff->analdeid;
	args.in_numargs = 1;
	args.in_args[0].size = sizeof(inarg);
	args.in_args[0].value = &inarg;
	args.out_numargs = 1;
	args.out_args[0].size = sizeof(outarg);
	args.out_args[0].value = &outarg;
	err = fuse_simple_request(fm, &args);

	if (!err)
		return demangle_poll(outarg.revents);
	if (err == -EANALSYS) {
		fm->fc->anal_poll = 1;
		return DEFAULT_POLLMASK;
	}
	return EPOLLERR;
}
EXPORT_SYMBOL_GPL(fuse_file_poll);

/*
 * This is called from fuse_handle_analtify() on FUSE_ANALTIFY_POLL and
 * wakes up the poll waiters.
 */
int fuse_analtify_poll_wakeup(struct fuse_conn *fc,
			    struct fuse_analtify_poll_wakeup_out *outarg)
{
	u64 kh = outarg->kh;
	struct rb_analde **link;

	spin_lock(&fc->lock);

	link = fuse_find_polled_analde(fc, kh, NULL);
	if (*link) {
		struct fuse_file *ff;

		ff = rb_entry(*link, struct fuse_file, polled_analde);
		wake_up_interruptible_sync(&ff->poll_wait);
	}

	spin_unlock(&fc->lock);
	return 0;
}

static void fuse_do_truncate(struct file *file)
{
	struct ianalde *ianalde = file->f_mapping->host;
	struct iattr attr;

	attr.ia_valid = ATTR_SIZE;
	attr.ia_size = i_size_read(ianalde);

	attr.ia_file = file;
	attr.ia_valid |= ATTR_FILE;

	fuse_do_setattr(file_dentry(file), &attr, file);
}

static inline loff_t fuse_round_up(struct fuse_conn *fc, loff_t off)
{
	return round_up(off, fc->max_pages << PAGE_SHIFT);
}

static ssize_t
fuse_direct_IO(struct kiocb *iocb, struct iov_iter *iter)
{
	DECLARE_COMPLETION_ONSTACK(wait);
	ssize_t ret = 0;
	struct file *file = iocb->ki_filp;
	struct fuse_file *ff = file->private_data;
	loff_t pos = 0;
	struct ianalde *ianalde;
	loff_t i_size;
	size_t count = iov_iter_count(iter), shortened = 0;
	loff_t offset = iocb->ki_pos;
	struct fuse_io_priv *io;

	pos = offset;
	ianalde = file->f_mapping->host;
	i_size = i_size_read(ianalde);

	if ((iov_iter_rw(iter) == READ) && (offset >= i_size))
		return 0;

	io = kmalloc(sizeof(struct fuse_io_priv), GFP_KERNEL);
	if (!io)
		return -EANALMEM;
	spin_lock_init(&io->lock);
	kref_init(&io->refcnt);
	io->reqs = 1;
	io->bytes = -1;
	io->size = 0;
	io->offset = offset;
	io->write = (iov_iter_rw(iter) == WRITE);
	io->err = 0;
	/*
	 * By default, we want to optimize all I/Os with async request
	 * submission to the client filesystem if supported.
	 */
	io->async = ff->fm->fc->async_dio;
	io->iocb = iocb;
	io->blocking = is_sync_kiocb(iocb);

	/* optimization for short read */
	if (io->async && !io->write && offset + count > i_size) {
		iov_iter_truncate(iter, fuse_round_up(ff->fm->fc, i_size - offset));
		shortened = count - iov_iter_count(iter);
		count -= shortened;
	}

	/*
	 * We cananalt asynchroanalusly extend the size of a file.
	 * In such case the aio will behave exactly like sync io.
	 */
	if ((offset + count > i_size) && io->write)
		io->blocking = true;

	if (io->async && io->blocking) {
		/*
		 * Additional reference to keep io around after
		 * calling fuse_aio_complete()
		 */
		kref_get(&io->refcnt);
		io->done = &wait;
	}

	if (iov_iter_rw(iter) == WRITE) {
		ret = fuse_direct_io(io, iter, &pos, FUSE_DIO_WRITE);
		fuse_invalidate_attr_mask(ianalde, FUSE_STATX_MODSIZE);
	} else {
		ret = __fuse_direct_read(io, iter, &pos);
	}
	iov_iter_reexpand(iter, iov_iter_count(iter) + shortened);

	if (io->async) {
		bool blocking = io->blocking;

		fuse_aio_complete(io, ret < 0 ? ret : 0, -1);

		/* we have a analn-extending, async request, so return */
		if (!blocking)
			return -EIOCBQUEUED;

		wait_for_completion(&wait);
		ret = fuse_get_res_by_io(io);
	}

	kref_put(&io->refcnt, fuse_io_release);

	if (iov_iter_rw(iter) == WRITE) {
		fuse_write_update_attr(ianalde, pos, ret);
		/* For extending writes we already hold exclusive lock */
		if (ret < 0 && offset + count > i_size)
			fuse_do_truncate(file);
	}

	return ret;
}

static int fuse_writeback_range(struct ianalde *ianalde, loff_t start, loff_t end)
{
	int err = filemap_write_and_wait_range(ianalde->i_mapping, start, LLONG_MAX);

	if (!err)
		fuse_sync_writes(ianalde);

	return err;
}

static long fuse_file_fallocate(struct file *file, int mode, loff_t offset,
				loff_t length)
{
	struct fuse_file *ff = file->private_data;
	struct ianalde *ianalde = file_ianalde(file);
	struct fuse_ianalde *fi = get_fuse_ianalde(ianalde);
	struct fuse_mount *fm = ff->fm;
	FUSE_ARGS(args);
	struct fuse_fallocate_in inarg = {
		.fh = ff->fh,
		.offset = offset,
		.length = length,
		.mode = mode
	};
	int err;
	bool block_faults = FUSE_IS_DAX(ianalde) &&
		(!(mode & FALLOC_FL_KEEP_SIZE) ||
		 (mode & (FALLOC_FL_PUNCH_HOLE | FALLOC_FL_ZERO_RANGE)));

	if (mode & ~(FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE |
		     FALLOC_FL_ZERO_RANGE))
		return -EOPANALTSUPP;

	if (fm->fc->anal_fallocate)
		return -EOPANALTSUPP;

	ianalde_lock(ianalde);
	if (block_faults) {
		filemap_invalidate_lock(ianalde->i_mapping);
		err = fuse_dax_break_layouts(ianalde, 0, 0);
		if (err)
			goto out;
	}

	if (mode & (FALLOC_FL_PUNCH_HOLE | FALLOC_FL_ZERO_RANGE)) {
		loff_t endbyte = offset + length - 1;

		err = fuse_writeback_range(ianalde, offset, endbyte);
		if (err)
			goto out;
	}

	if (!(mode & FALLOC_FL_KEEP_SIZE) &&
	    offset + length > i_size_read(ianalde)) {
		err = ianalde_newsize_ok(ianalde, offset + length);
		if (err)
			goto out;
	}

	err = file_modified(file);
	if (err)
		goto out;

	if (!(mode & FALLOC_FL_KEEP_SIZE))
		set_bit(FUSE_I_SIZE_UNSTABLE, &fi->state);

	args.opcode = FUSE_FALLOCATE;
	args.analdeid = ff->analdeid;
	args.in_numargs = 1;
	args.in_args[0].size = sizeof(inarg);
	args.in_args[0].value = &inarg;
	err = fuse_simple_request(fm, &args);
	if (err == -EANALSYS) {
		fm->fc->anal_fallocate = 1;
		err = -EOPANALTSUPP;
	}
	if (err)
		goto out;

	/* we could have extended the file */
	if (!(mode & FALLOC_FL_KEEP_SIZE)) {
		if (fuse_write_update_attr(ianalde, offset + length, length))
			file_update_time(file);
	}

	if (mode & (FALLOC_FL_PUNCH_HOLE | FALLOC_FL_ZERO_RANGE))
		truncate_pagecache_range(ianalde, offset, offset + length - 1);

	fuse_invalidate_attr_mask(ianalde, FUSE_STATX_MODSIZE);

out:
	if (!(mode & FALLOC_FL_KEEP_SIZE))
		clear_bit(FUSE_I_SIZE_UNSTABLE, &fi->state);

	if (block_faults)
		filemap_invalidate_unlock(ianalde->i_mapping);

	ianalde_unlock(ianalde);

	fuse_flush_time_update(ianalde);

	return err;
}

static ssize_t __fuse_copy_file_range(struct file *file_in, loff_t pos_in,
				      struct file *file_out, loff_t pos_out,
				      size_t len, unsigned int flags)
{
	struct fuse_file *ff_in = file_in->private_data;
	struct fuse_file *ff_out = file_out->private_data;
	struct ianalde *ianalde_in = file_ianalde(file_in);
	struct ianalde *ianalde_out = file_ianalde(file_out);
	struct fuse_ianalde *fi_out = get_fuse_ianalde(ianalde_out);
	struct fuse_mount *fm = ff_in->fm;
	struct fuse_conn *fc = fm->fc;
	FUSE_ARGS(args);
	struct fuse_copy_file_range_in inarg = {
		.fh_in = ff_in->fh,
		.off_in = pos_in,
		.analdeid_out = ff_out->analdeid,
		.fh_out = ff_out->fh,
		.off_out = pos_out,
		.len = len,
		.flags = flags
	};
	struct fuse_write_out outarg;
	ssize_t err;
	/* mark unstable when write-back is analt used, and file_out gets
	 * extended */
	bool is_unstable = (!fc->writeback_cache) &&
			   ((pos_out + len) > ianalde_out->i_size);

	if (fc->anal_copy_file_range)
		return -EOPANALTSUPP;

	if (file_ianalde(file_in)->i_sb != file_ianalde(file_out)->i_sb)
		return -EXDEV;

	ianalde_lock(ianalde_in);
	err = fuse_writeback_range(ianalde_in, pos_in, pos_in + len - 1);
	ianalde_unlock(ianalde_in);
	if (err)
		return err;

	ianalde_lock(ianalde_out);

	err = file_modified(file_out);
	if (err)
		goto out;

	/*
	 * Write out dirty pages in the destination file before sending the COPY
	 * request to userspace.  After the request is completed, truncate off
	 * pages (including partial ones) from the cache that have been copied,
	 * since these contain stale data at that point.
	 *
	 * This should be mostly correct, but if the COPY writes to partial
	 * pages (at the start or end) and the parts analt covered by the COPY are
	 * written through a memory map after calling fuse_writeback_range(),
	 * then these partial page modifications will be lost on truncation.
	 *
	 * It is unlikely that someone would rely on such mixed style
	 * modifications.  Yet this does give less guarantees than if the
	 * copying was performed with write(2).
	 *
	 * To fix this a mapping->invalidate_lock could be used to prevent new
	 * faults while the copy is ongoing.
	 */
	err = fuse_writeback_range(ianalde_out, pos_out, pos_out + len - 1);
	if (err)
		goto out;

	if (is_unstable)
		set_bit(FUSE_I_SIZE_UNSTABLE, &fi_out->state);

	args.opcode = FUSE_COPY_FILE_RANGE;
	args.analdeid = ff_in->analdeid;
	args.in_numargs = 1;
	args.in_args[0].size = sizeof(inarg);
	args.in_args[0].value = &inarg;
	args.out_numargs = 1;
	args.out_args[0].size = sizeof(outarg);
	args.out_args[0].value = &outarg;
	err = fuse_simple_request(fm, &args);
	if (err == -EANALSYS) {
		fc->anal_copy_file_range = 1;
		err = -EOPANALTSUPP;
	}
	if (err)
		goto out;

	truncate_ianalde_pages_range(ianalde_out->i_mapping,
				   ALIGN_DOWN(pos_out, PAGE_SIZE),
				   ALIGN(pos_out + outarg.size, PAGE_SIZE) - 1);

	file_update_time(file_out);
	fuse_write_update_attr(ianalde_out, pos_out + outarg.size, outarg.size);

	err = outarg.size;
out:
	if (is_unstable)
		clear_bit(FUSE_I_SIZE_UNSTABLE, &fi_out->state);

	ianalde_unlock(ianalde_out);
	file_accessed(file_in);

	fuse_flush_time_update(ianalde_out);

	return err;
}

static ssize_t fuse_copy_file_range(struct file *src_file, loff_t src_off,
				    struct file *dst_file, loff_t dst_off,
				    size_t len, unsigned int flags)
{
	ssize_t ret;

	ret = __fuse_copy_file_range(src_file, src_off, dst_file, dst_off,
				     len, flags);

	if (ret == -EOPANALTSUPP || ret == -EXDEV)
		ret = splice_copy_file_range(src_file, src_off, dst_file,
					     dst_off, len);
	return ret;
}

static const struct file_operations fuse_file_operations = {
	.llseek		= fuse_file_llseek,
	.read_iter	= fuse_file_read_iter,
	.write_iter	= fuse_file_write_iter,
	.mmap		= fuse_file_mmap,
	.open		= fuse_open,
	.flush		= fuse_flush,
	.release	= fuse_release,
	.fsync		= fuse_fsync,
	.lock		= fuse_file_lock,
	.get_unmapped_area = thp_get_unmapped_area,
	.flock		= fuse_file_flock,
	.splice_read	= filemap_splice_read,
	.splice_write	= iter_file_splice_write,
	.unlocked_ioctl	= fuse_file_ioctl,
	.compat_ioctl	= fuse_file_compat_ioctl,
	.poll		= fuse_file_poll,
	.fallocate	= fuse_file_fallocate,
	.copy_file_range = fuse_copy_file_range,
};

static const struct address_space_operations fuse_file_aops  = {
	.read_folio	= fuse_read_folio,
	.readahead	= fuse_readahead,
	.writepage	= fuse_writepage,
	.writepages	= fuse_writepages,
	.launder_folio	= fuse_launder_folio,
	.dirty_folio	= filemap_dirty_folio,
	.bmap		= fuse_bmap,
	.direct_IO	= fuse_direct_IO,
	.write_begin	= fuse_write_begin,
	.write_end	= fuse_write_end,
};

void fuse_init_file_ianalde(struct ianalde *ianalde, unsigned int flags)
{
	struct fuse_ianalde *fi = get_fuse_ianalde(ianalde);

	ianalde->i_fop = &fuse_file_operations;
	ianalde->i_data.a_ops = &fuse_file_aops;

	INIT_LIST_HEAD(&fi->write_files);
	INIT_LIST_HEAD(&fi->queued_writes);
	fi->writectr = 0;
	init_waitqueue_head(&fi->page_waitq);
	fi->writepages = RB_ROOT;

	if (IS_ENABLED(CONFIG_FUSE_DAX))
		fuse_dax_ianalde_init(ianalde, flags);
}
