/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2006  Miklos Szeredi <miklos@szeredi.hu>

  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.
*/

#include "fuse_i.h"

#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/sched.h>

static const struct file_operations fuse_direct_io_file_operations;

static int fuse_send_open(struct inode *inode, struct file *file, int isdir,
			  struct fuse_open_out *outargp)
{
	struct fuse_conn *fc = get_fuse_conn(inode);
	struct fuse_open_in inarg;
	struct fuse_req *req;
	int err;

	req = fuse_get_req(fc);
	if (IS_ERR(req))
		return PTR_ERR(req);

	memset(&inarg, 0, sizeof(inarg));
	inarg.flags = file->f_flags & ~(O_CREAT | O_EXCL | O_NOCTTY | O_TRUNC);
	req->in.h.opcode = isdir ? FUSE_OPENDIR : FUSE_OPEN;
	req->in.h.nodeid = get_node_id(inode);
	req->in.numargs = 1;
	req->in.args[0].size = sizeof(inarg);
	req->in.args[0].value = &inarg;
	req->out.numargs = 1;
	req->out.args[0].size = sizeof(*outargp);
	req->out.args[0].value = outargp;
	request_send(fc, req);
	err = req->out.h.error;
	fuse_put_request(fc, req);

	return err;
}

struct fuse_file *fuse_file_alloc(void)
{
	struct fuse_file *ff;
	ff = kmalloc(sizeof(struct fuse_file), GFP_KERNEL);
	if (ff) {
		ff->reserved_req = fuse_request_alloc();
		if (!ff->reserved_req) {
			kfree(ff);
			ff = NULL;
		}
		atomic_set(&ff->count, 0);
	}
	return ff;
}

void fuse_file_free(struct fuse_file *ff)
{
	fuse_request_free(ff->reserved_req);
	kfree(ff);
}

static struct fuse_file *fuse_file_get(struct fuse_file *ff)
{
	atomic_inc(&ff->count);
	return ff;
}

static void fuse_release_end(struct fuse_conn *fc, struct fuse_req *req)
{
	dput(req->dentry);
	mntput(req->vfsmount);
	fuse_put_request(fc, req);
}

static void fuse_file_put(struct fuse_file *ff)
{
	if (atomic_dec_and_test(&ff->count)) {
		struct fuse_req *req = ff->reserved_req;
		struct fuse_conn *fc = get_fuse_conn(req->dentry->d_inode);
		req->end = fuse_release_end;
		request_send_background(fc, req);
		kfree(ff);
	}
}

void fuse_finish_open(struct inode *inode, struct file *file,
		      struct fuse_file *ff, struct fuse_open_out *outarg)
{
	if (outarg->open_flags & FOPEN_DIRECT_IO)
		file->f_op = &fuse_direct_io_file_operations;
	if (!(outarg->open_flags & FOPEN_KEEP_CACHE))
		invalidate_inode_pages2(inode->i_mapping);
	ff->fh = outarg->fh;
	file->private_data = fuse_file_get(ff);
}

int fuse_open_common(struct inode *inode, struct file *file, int isdir)
{
	struct fuse_open_out outarg;
	struct fuse_file *ff;
	int err;

	/* VFS checks this, but only _after_ ->open() */
	if (file->f_flags & O_DIRECT)
		return -EINVAL;

	err = generic_file_open(inode, file);
	if (err)
		return err;

	ff = fuse_file_alloc();
	if (!ff)
		return -ENOMEM;

	err = fuse_send_open(inode, file, isdir, &outarg);
	if (err)
		fuse_file_free(ff);
	else {
		if (isdir)
			outarg.open_flags &= ~FOPEN_DIRECT_IO;
		fuse_finish_open(inode, file, ff, &outarg);
	}

	return err;
}

void fuse_release_fill(struct fuse_file *ff, u64 nodeid, int flags, int opcode)
{
	struct fuse_req *req = ff->reserved_req;
	struct fuse_release_in *inarg = &req->misc.release_in;

	inarg->fh = ff->fh;
	inarg->flags = flags;
	req->in.h.opcode = opcode;
	req->in.h.nodeid = nodeid;
	req->in.numargs = 1;
	req->in.args[0].size = sizeof(struct fuse_release_in);
	req->in.args[0].value = inarg;
}

int fuse_release_common(struct inode *inode, struct file *file, int isdir)
{
	struct fuse_file *ff = file->private_data;
	if (ff) {
		fuse_release_fill(ff, get_node_id(inode), file->f_flags,
				  isdir ? FUSE_RELEASEDIR : FUSE_RELEASE);

		/* Hold vfsmount and dentry until release is finished */
		ff->reserved_req->vfsmount = mntget(file->f_path.mnt);
		ff->reserved_req->dentry = dget(file->f_path.dentry);
		/*
		 * Normally this will send the RELEASE request,
		 * however if some asynchronous READ or WRITE requests
		 * are outstanding, the sending will be delayed
		 */
		fuse_file_put(ff);
	}

	/* Return value is ignored by VFS */
	return 0;
}

static int fuse_open(struct inode *inode, struct file *file)
{
	return fuse_open_common(inode, file, 0);
}

static int fuse_release(struct inode *inode, struct file *file)
{
	return fuse_release_common(inode, file, 0);
}

/*
 * Scramble the ID space with XTEA, so that the value of the files_struct
 * pointer is not exposed to userspace.
 */
static u64 fuse_lock_owner_id(struct fuse_conn *fc, fl_owner_t id)
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

static int fuse_flush(struct file *file, fl_owner_t id)
{
	struct inode *inode = file->f_path.dentry->d_inode;
	struct fuse_conn *fc = get_fuse_conn(inode);
	struct fuse_file *ff = file->private_data;
	struct fuse_req *req;
	struct fuse_flush_in inarg;
	int err;

	if (is_bad_inode(inode))
		return -EIO;

	if (fc->no_flush)
		return 0;

	req = fuse_get_req_nofail(fc, file);
	memset(&inarg, 0, sizeof(inarg));
	inarg.fh = ff->fh;
	inarg.lock_owner = fuse_lock_owner_id(fc, id);
	req->in.h.opcode = FUSE_FLUSH;
	req->in.h.nodeid = get_node_id(inode);
	req->in.numargs = 1;
	req->in.args[0].size = sizeof(inarg);
	req->in.args[0].value = &inarg;
	req->force = 1;
	request_send(fc, req);
	err = req->out.h.error;
	fuse_put_request(fc, req);
	if (err == -ENOSYS) {
		fc->no_flush = 1;
		err = 0;
	}
	return err;
}

int fuse_fsync_common(struct file *file, struct dentry *de, int datasync,
		      int isdir)
{
	struct inode *inode = de->d_inode;
	struct fuse_conn *fc = get_fuse_conn(inode);
	struct fuse_file *ff = file->private_data;
	struct fuse_req *req;
	struct fuse_fsync_in inarg;
	int err;

	if (is_bad_inode(inode))
		return -EIO;

	if ((!isdir && fc->no_fsync) || (isdir && fc->no_fsyncdir))
		return 0;

	req = fuse_get_req(fc);
	if (IS_ERR(req))
		return PTR_ERR(req);

	memset(&inarg, 0, sizeof(inarg));
	inarg.fh = ff->fh;
	inarg.fsync_flags = datasync ? 1 : 0;
	req->in.h.opcode = isdir ? FUSE_FSYNCDIR : FUSE_FSYNC;
	req->in.h.nodeid = get_node_id(inode);
	req->in.numargs = 1;
	req->in.args[0].size = sizeof(inarg);
	req->in.args[0].value = &inarg;
	request_send(fc, req);
	err = req->out.h.error;
	fuse_put_request(fc, req);
	if (err == -ENOSYS) {
		if (isdir)
			fc->no_fsyncdir = 1;
		else
			fc->no_fsync = 1;
		err = 0;
	}
	return err;
}

static int fuse_fsync(struct file *file, struct dentry *de, int datasync)
{
	return fuse_fsync_common(file, de, datasync, 0);
}

void fuse_read_fill(struct fuse_req *req, struct fuse_file *ff,
		    struct inode *inode, loff_t pos, size_t count, int opcode)
{
	struct fuse_read_in *inarg = &req->misc.read_in;

	inarg->fh = ff->fh;
	inarg->offset = pos;
	inarg->size = count;
	req->in.h.opcode = opcode;
	req->in.h.nodeid = get_node_id(inode);
	req->in.numargs = 1;
	req->in.args[0].size = sizeof(struct fuse_read_in);
	req->in.args[0].value = inarg;
	req->out.argpages = 1;
	req->out.argvar = 1;
	req->out.numargs = 1;
	req->out.args[0].size = count;
}

static size_t fuse_send_read(struct fuse_req *req, struct file *file,
			     struct inode *inode, loff_t pos, size_t count)
{
	struct fuse_conn *fc = get_fuse_conn(inode);
	struct fuse_file *ff = file->private_data;
	fuse_read_fill(req, ff, inode, pos, count, FUSE_READ);
	request_send(fc, req);
	return req->out.args[0].size;
}

static int fuse_readpage(struct file *file, struct page *page)
{
	struct inode *inode = page->mapping->host;
	struct fuse_conn *fc = get_fuse_conn(inode);
	struct fuse_req *req;
	int err;

	err = -EIO;
	if (is_bad_inode(inode))
		goto out;

	req = fuse_get_req(fc);
	err = PTR_ERR(req);
	if (IS_ERR(req))
		goto out;

	req->out.page_zeroing = 1;
	req->num_pages = 1;
	req->pages[0] = page;
	fuse_send_read(req, file, inode, page_offset(page), PAGE_CACHE_SIZE);
	err = req->out.h.error;
	fuse_put_request(fc, req);
	if (!err)
		SetPageUptodate(page);
	fuse_invalidate_attr(inode); /* atime changed */
 out:
	unlock_page(page);
	return err;
}

static void fuse_readpages_end(struct fuse_conn *fc, struct fuse_req *req)
{
	int i;

	fuse_invalidate_attr(req->pages[0]->mapping->host); /* atime changed */

	for (i = 0; i < req->num_pages; i++) {
		struct page *page = req->pages[i];
		if (!req->out.h.error)
			SetPageUptodate(page);
		else
			SetPageError(page);
		unlock_page(page);
	}
	if (req->ff)
		fuse_file_put(req->ff);
	fuse_put_request(fc, req);
}

static void fuse_send_readpages(struct fuse_req *req, struct fuse_file *ff,
				struct inode *inode)
{
	struct fuse_conn *fc = get_fuse_conn(inode);
	loff_t pos = page_offset(req->pages[0]);
	size_t count = req->num_pages << PAGE_CACHE_SHIFT;
	req->out.page_zeroing = 1;
	fuse_read_fill(req, ff, inode, pos, count, FUSE_READ);
	if (fc->async_read) {
		req->ff = fuse_file_get(ff);
		req->end = fuse_readpages_end;
		request_send_background(fc, req);
	} else {
		request_send(fc, req);
		fuse_readpages_end(fc, req);
	}
}

struct fuse_fill_data {
	struct fuse_req *req;
	struct fuse_file *ff;
	struct inode *inode;
};

static int fuse_readpages_fill(void *_data, struct page *page)
{
	struct fuse_fill_data *data = _data;
	struct fuse_req *req = data->req;
	struct inode *inode = data->inode;
	struct fuse_conn *fc = get_fuse_conn(inode);

	if (req->num_pages &&
	    (req->num_pages == FUSE_MAX_PAGES_PER_REQ ||
	     (req->num_pages + 1) * PAGE_CACHE_SIZE > fc->max_read ||
	     req->pages[req->num_pages - 1]->index + 1 != page->index)) {
		fuse_send_readpages(req, data->ff, inode);
		data->req = req = fuse_get_req(fc);
		if (IS_ERR(req)) {
			unlock_page(page);
			return PTR_ERR(req);
		}
	}
	req->pages[req->num_pages] = page;
	req->num_pages ++;
	return 0;
}

static int fuse_readpages(struct file *file, struct address_space *mapping,
			  struct list_head *pages, unsigned nr_pages)
{
	struct inode *inode = mapping->host;
	struct fuse_conn *fc = get_fuse_conn(inode);
	struct fuse_fill_data data;
	int err;

	err = -EIO;
	if (is_bad_inode(inode))
		goto out;

	data.ff = file->private_data;
	data.inode = inode;
	data.req = fuse_get_req(fc);
	err = PTR_ERR(data.req);
	if (IS_ERR(data.req))
		goto out;

	err = read_cache_pages(mapping, pages, fuse_readpages_fill, &data);
	if (!err) {
		if (data.req->num_pages)
			fuse_send_readpages(data.req, data.ff, inode);
		else
			fuse_put_request(fc, data.req);
	}
out:
	return err;
}

static size_t fuse_send_write(struct fuse_req *req, struct file *file,
			      struct inode *inode, loff_t pos, size_t count)
{
	struct fuse_conn *fc = get_fuse_conn(inode);
	struct fuse_file *ff = file->private_data;
	struct fuse_write_in inarg;
	struct fuse_write_out outarg;

	memset(&inarg, 0, sizeof(struct fuse_write_in));
	inarg.fh = ff->fh;
	inarg.offset = pos;
	inarg.size = count;
	req->in.h.opcode = FUSE_WRITE;
	req->in.h.nodeid = get_node_id(inode);
	req->in.argpages = 1;
	req->in.numargs = 2;
	req->in.args[0].size = sizeof(struct fuse_write_in);
	req->in.args[0].value = &inarg;
	req->in.args[1].size = count;
	req->out.numargs = 1;
	req->out.args[0].size = sizeof(struct fuse_write_out);
	req->out.args[0].value = &outarg;
	request_send(fc, req);
	return outarg.size;
}

static int fuse_write_begin(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned flags,
			struct page **pagep, void **fsdata)
{
	pgoff_t index = pos >> PAGE_CACHE_SHIFT;

	*pagep = __grab_cache_page(mapping, index);
	if (!*pagep)
		return -ENOMEM;
	return 0;
}

static int fuse_buffered_write(struct file *file, struct inode *inode,
			       loff_t pos, unsigned count, struct page *page)
{
	int err;
	size_t nres;
	struct fuse_conn *fc = get_fuse_conn(inode);
	struct fuse_inode *fi = get_fuse_inode(inode);
	unsigned offset = pos & (PAGE_CACHE_SIZE - 1);
	struct fuse_req *req;

	if (is_bad_inode(inode))
		return -EIO;

	req = fuse_get_req(fc);
	if (IS_ERR(req))
		return PTR_ERR(req);

	req->num_pages = 1;
	req->pages[0] = page;
	req->page_offset = offset;
	nres = fuse_send_write(req, file, inode, pos, count);
	err = req->out.h.error;
	fuse_put_request(fc, req);
	if (!err && !nres)
		err = -EIO;
	if (!err) {
		pos += nres;
		spin_lock(&fc->lock);
		fi->attr_version = ++fc->attr_version;
		if (pos > inode->i_size)
			i_size_write(inode, pos);
		spin_unlock(&fc->lock);

		if (count == PAGE_CACHE_SIZE)
			SetPageUptodate(page);
	}
	fuse_invalidate_attr(inode);
	return err ? err : nres;
}

static int fuse_write_end(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned copied,
			struct page *page, void *fsdata)
{
	struct inode *inode = mapping->host;
	int res = 0;

	if (copied)
		res = fuse_buffered_write(file, inode, pos, copied, page);

	unlock_page(page);
	page_cache_release(page);
	return res;
}

static void fuse_release_user_pages(struct fuse_req *req, int write)
{
	unsigned i;

	for (i = 0; i < req->num_pages; i++) {
		struct page *page = req->pages[i];
		if (write)
			set_page_dirty_lock(page);
		put_page(page);
	}
}

static int fuse_get_user_pages(struct fuse_req *req, const char __user *buf,
			       unsigned nbytes, int write)
{
	unsigned long user_addr = (unsigned long) buf;
	unsigned offset = user_addr & ~PAGE_MASK;
	int npages;

	/* This doesn't work with nfsd */
	if (!current->mm)
		return -EPERM;

	nbytes = min(nbytes, (unsigned) FUSE_MAX_PAGES_PER_REQ << PAGE_SHIFT);
	npages = (nbytes + offset + PAGE_SIZE - 1) >> PAGE_SHIFT;
	npages = min(max(npages, 1), FUSE_MAX_PAGES_PER_REQ);
	down_read(&current->mm->mmap_sem);
	npages = get_user_pages(current, current->mm, user_addr, npages, write,
				0, req->pages, NULL);
	up_read(&current->mm->mmap_sem);
	if (npages < 0)
		return npages;

	req->num_pages = npages;
	req->page_offset = offset;
	return 0;
}

static ssize_t fuse_direct_io(struct file *file, const char __user *buf,
			      size_t count, loff_t *ppos, int write)
{
	struct inode *inode = file->f_path.dentry->d_inode;
	struct fuse_conn *fc = get_fuse_conn(inode);
	size_t nmax = write ? fc->max_write : fc->max_read;
	loff_t pos = *ppos;
	ssize_t res = 0;
	struct fuse_req *req;

	if (is_bad_inode(inode))
		return -EIO;

	req = fuse_get_req(fc);
	if (IS_ERR(req))
		return PTR_ERR(req);

	while (count) {
		size_t nres;
		size_t nbytes = min(count, nmax);
		int err = fuse_get_user_pages(req, buf, nbytes, !write);
		if (err) {
			res = err;
			break;
		}
		nbytes = (req->num_pages << PAGE_SHIFT) - req->page_offset;
		nbytes = min(count, nbytes);
		if (write)
			nres = fuse_send_write(req, file, inode, pos, nbytes);
		else
			nres = fuse_send_read(req, file, inode, pos, nbytes);
		fuse_release_user_pages(req, !write);
		if (req->out.h.error) {
			if (!res)
				res = req->out.h.error;
			break;
		} else if (nres > nbytes) {
			res = -EIO;
			break;
		}
		count -= nres;
		res += nres;
		pos += nres;
		buf += nres;
		if (nres != nbytes)
			break;
		if (count) {
			fuse_put_request(fc, req);
			req = fuse_get_req(fc);
			if (IS_ERR(req))
				break;
		}
	}
	fuse_put_request(fc, req);
	if (res > 0) {
		if (write) {
			spin_lock(&fc->lock);
			if (pos > inode->i_size)
				i_size_write(inode, pos);
			spin_unlock(&fc->lock);
		}
		*ppos = pos;
	}
	fuse_invalidate_attr(inode);

	return res;
}

static ssize_t fuse_direct_read(struct file *file, char __user *buf,
				     size_t count, loff_t *ppos)
{
	return fuse_direct_io(file, buf, count, ppos, 0);
}

static ssize_t fuse_direct_write(struct file *file, const char __user *buf,
				 size_t count, loff_t *ppos)
{
	struct inode *inode = file->f_path.dentry->d_inode;
	ssize_t res;
	/* Don't allow parallel writes to the same file */
	mutex_lock(&inode->i_mutex);
	res = generic_write_checks(file, ppos, &count, 0);
	if (!res)
		res = fuse_direct_io(file, buf, count, ppos, 1);
	mutex_unlock(&inode->i_mutex);
	return res;
}

static int fuse_file_mmap(struct file *file, struct vm_area_struct *vma)
{
	if ((vma->vm_flags & VM_SHARED)) {
		if ((vma->vm_flags & VM_WRITE))
			return -ENODEV;
		else
			vma->vm_flags &= ~VM_MAYWRITE;
	}
	return generic_file_mmap(file, vma);
}

static int fuse_set_page_dirty(struct page *page)
{
	printk("fuse_set_page_dirty: should not happen\n");
	dump_stack();
	return 0;
}

static int convert_fuse_file_lock(const struct fuse_file_lock *ffl,
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
		fl->fl_pid = ffl->pid;
		break;

	default:
		return -EIO;
	}
	fl->fl_type = ffl->type;
	return 0;
}

static void fuse_lk_fill(struct fuse_req *req, struct file *file,
			 const struct file_lock *fl, int opcode, pid_t pid)
{
	struct inode *inode = file->f_path.dentry->d_inode;
	struct fuse_conn *fc = get_fuse_conn(inode);
	struct fuse_file *ff = file->private_data;
	struct fuse_lk_in *arg = &req->misc.lk_in;

	arg->fh = ff->fh;
	arg->owner = fuse_lock_owner_id(fc, fl->fl_owner);
	arg->lk.start = fl->fl_start;
	arg->lk.end = fl->fl_end;
	arg->lk.type = fl->fl_type;
	arg->lk.pid = pid;
	req->in.h.opcode = opcode;
	req->in.h.nodeid = get_node_id(inode);
	req->in.numargs = 1;
	req->in.args[0].size = sizeof(*arg);
	req->in.args[0].value = arg;
}

static int fuse_getlk(struct file *file, struct file_lock *fl)
{
	struct inode *inode = file->f_path.dentry->d_inode;
	struct fuse_conn *fc = get_fuse_conn(inode);
	struct fuse_req *req;
	struct fuse_lk_out outarg;
	int err;

	req = fuse_get_req(fc);
	if (IS_ERR(req))
		return PTR_ERR(req);

	fuse_lk_fill(req, file, fl, FUSE_GETLK, 0);
	req->out.numargs = 1;
	req->out.args[0].size = sizeof(outarg);
	req->out.args[0].value = &outarg;
	request_send(fc, req);
	err = req->out.h.error;
	fuse_put_request(fc, req);
	if (!err)
		err = convert_fuse_file_lock(&outarg.lk, fl);

	return err;
}

static int fuse_setlk(struct file *file, struct file_lock *fl)
{
	struct inode *inode = file->f_path.dentry->d_inode;
	struct fuse_conn *fc = get_fuse_conn(inode);
	struct fuse_req *req;
	int opcode = (fl->fl_flags & FL_SLEEP) ? FUSE_SETLKW : FUSE_SETLK;
	pid_t pid = fl->fl_type != F_UNLCK ? current->tgid : 0;
	int err;

	/* Unlock on close is handled by the flush method */
	if (fl->fl_flags & FL_CLOSE)
		return 0;

	req = fuse_get_req(fc);
	if (IS_ERR(req))
		return PTR_ERR(req);

	fuse_lk_fill(req, file, fl, opcode, pid);
	request_send(fc, req);
	err = req->out.h.error;
	/* locking is restartable */
	if (err == -EINTR)
		err = -ERESTARTSYS;
	fuse_put_request(fc, req);
	return err;
}

static int fuse_file_lock(struct file *file, int cmd, struct file_lock *fl)
{
	struct inode *inode = file->f_path.dentry->d_inode;
	struct fuse_conn *fc = get_fuse_conn(inode);
	int err;

	if (cmd == F_GETLK) {
		if (fc->no_lock) {
			posix_test_lock(file, fl);
			err = 0;
		} else
			err = fuse_getlk(file, fl);
	} else {
		if (fc->no_lock)
			err = posix_lock_file_wait(file, fl);
		else
			err = fuse_setlk(file, fl);
	}
	return err;
}

static sector_t fuse_bmap(struct address_space *mapping, sector_t block)
{
	struct inode *inode = mapping->host;
	struct fuse_conn *fc = get_fuse_conn(inode);
	struct fuse_req *req;
	struct fuse_bmap_in inarg;
	struct fuse_bmap_out outarg;
	int err;

	if (!inode->i_sb->s_bdev || fc->no_bmap)
		return 0;

	req = fuse_get_req(fc);
	if (IS_ERR(req))
		return 0;

	memset(&inarg, 0, sizeof(inarg));
	inarg.block = block;
	inarg.blocksize = inode->i_sb->s_blocksize;
	req->in.h.opcode = FUSE_BMAP;
	req->in.h.nodeid = get_node_id(inode);
	req->in.numargs = 1;
	req->in.args[0].size = sizeof(inarg);
	req->in.args[0].value = &inarg;
	req->out.numargs = 1;
	req->out.args[0].size = sizeof(outarg);
	req->out.args[0].value = &outarg;
	request_send(fc, req);
	err = req->out.h.error;
	fuse_put_request(fc, req);
	if (err == -ENOSYS)
		fc->no_bmap = 1;

	return err ? 0 : outarg.block;
}

static const struct file_operations fuse_file_operations = {
	.llseek		= generic_file_llseek,
	.read		= do_sync_read,
	.aio_read	= generic_file_aio_read,
	.write		= do_sync_write,
	.aio_write	= generic_file_aio_write,
	.mmap		= fuse_file_mmap,
	.open		= fuse_open,
	.flush		= fuse_flush,
	.release	= fuse_release,
	.fsync		= fuse_fsync,
	.lock		= fuse_file_lock,
	.splice_read	= generic_file_splice_read,
};

static const struct file_operations fuse_direct_io_file_operations = {
	.llseek		= generic_file_llseek,
	.read		= fuse_direct_read,
	.write		= fuse_direct_write,
	.open		= fuse_open,
	.flush		= fuse_flush,
	.release	= fuse_release,
	.fsync		= fuse_fsync,
	.lock		= fuse_file_lock,
	/* no mmap and splice_read */
};

static const struct address_space_operations fuse_file_aops  = {
	.readpage	= fuse_readpage,
	.write_begin	= fuse_write_begin,
	.write_end	= fuse_write_end,
	.readpages	= fuse_readpages,
	.set_page_dirty	= fuse_set_page_dirty,
	.bmap		= fuse_bmap,
};

void fuse_init_file_inode(struct inode *inode)
{
	inode->i_fop = &fuse_file_operations;
	inode->i_data.a_ops = &fuse_file_aops;
}
