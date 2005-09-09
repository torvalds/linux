/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2005  Miklos Szeredi <miklos@szeredi.hu>

  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.
*/

#include "fuse_i.h"

#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/kernel.h>

static int fuse_open(struct inode *inode, struct file *file)
{
	struct fuse_conn *fc = get_fuse_conn(inode);
	struct fuse_req *req;
	struct fuse_open_in inarg;
	struct fuse_open_out outarg;
	struct fuse_file *ff;
	int err;
	/* Restarting the syscall is not allowed if O_CREAT and O_EXCL
	   are both set, because creation will fail on the restart */
	int excl = (file->f_flags & (O_CREAT|O_EXCL)) == (O_CREAT|O_EXCL);

	err = generic_file_open(inode, file);
	if (err)
		return err;

	/* If opening the root node, no lookup has been performed on
	   it, so the attributes must be refreshed */
	if (get_node_id(inode) == FUSE_ROOT_ID) {
		int err = fuse_do_getattr(inode);
		if (err)
		 	return err;
	}

	if (excl)
		req = fuse_get_request_nonint(fc);
	else
		req = fuse_get_request(fc);
	if (!req)
		return excl ? -EINTR : -ERESTARTSYS;

	err = -ENOMEM;
	ff = kmalloc(sizeof(struct fuse_file), GFP_KERNEL);
	if (!ff)
		goto out_put_request;

	ff->release_req = fuse_request_alloc();
	if (!ff->release_req) {
		kfree(ff);
		goto out_put_request;
	}

	memset(&inarg, 0, sizeof(inarg));
	inarg.flags = file->f_flags & ~(O_CREAT | O_EXCL | O_NOCTTY | O_TRUNC);
	req->in.h.opcode = FUSE_OPEN;
	req->in.h.nodeid = get_node_id(inode);
	req->inode = inode;
	req->in.numargs = 1;
	req->in.args[0].size = sizeof(inarg);
	req->in.args[0].value = &inarg;
	req->out.numargs = 1;
	req->out.args[0].size = sizeof(outarg);
	req->out.args[0].value = &outarg;
	if (excl)
		request_send_nonint(fc, req);
	else
		request_send(fc, req);
	err = req->out.h.error;
	if (!err && !(fc->flags & FUSE_KERNEL_CACHE))
		invalidate_inode_pages(inode->i_mapping);
	if (err) {
		fuse_request_free(ff->release_req);
		kfree(ff);
	} else {
		ff->fh = outarg.fh;
		file->private_data = ff;
	}

 out_put_request:
	fuse_put_request(fc, req);
	return err;
}

static int fuse_release(struct inode *inode, struct file *file)
{
	struct fuse_conn *fc = get_fuse_conn(inode);
	struct fuse_file *ff = file->private_data;
	struct fuse_req *req = ff->release_req;
	struct fuse_release_in *inarg = &req->misc.release_in;

	inarg->fh = ff->fh;
	inarg->flags = file->f_flags & ~O_EXCL;
	req->in.h.opcode = FUSE_RELEASE;
	req->in.h.nodeid = get_node_id(inode);
	req->inode = inode;
	req->in.numargs = 1;
	req->in.args[0].size = sizeof(struct fuse_release_in);
	req->in.args[0].value = inarg;
	request_send_background(fc, req);
	kfree(ff);

	/* Return value is ignored by VFS */
	return 0;
}

static int fuse_flush(struct file *file)
{
	struct inode *inode = file->f_dentry->d_inode;
	struct fuse_conn *fc = get_fuse_conn(inode);
	struct fuse_file *ff = file->private_data;
	struct fuse_req *req;
	struct fuse_flush_in inarg;
	int err;

	if (fc->no_flush)
		return 0;

	req = fuse_get_request_nonint(fc);
	if (!req)
		return -EINTR;

	memset(&inarg, 0, sizeof(inarg));
	inarg.fh = ff->fh;
	req->in.h.opcode = FUSE_FLUSH;
	req->in.h.nodeid = get_node_id(inode);
	req->inode = inode;
	req->file = file;
	req->in.numargs = 1;
	req->in.args[0].size = sizeof(inarg);
	req->in.args[0].value = &inarg;
	request_send_nonint(fc, req);
	err = req->out.h.error;
	fuse_put_request(fc, req);
	if (err == -ENOSYS) {
		fc->no_flush = 1;
		err = 0;
	}
	return err;
}

static int fuse_fsync(struct file *file, struct dentry *de, int datasync)
{
	struct inode *inode = de->d_inode;
	struct fuse_conn *fc = get_fuse_conn(inode);
	struct fuse_file *ff = file->private_data;
	struct fuse_req *req;
	struct fuse_fsync_in inarg;
	int err;

	if (fc->no_fsync)
		return 0;

	req = fuse_get_request(fc);
	if (!req)
		return -ERESTARTSYS;

	memset(&inarg, 0, sizeof(inarg));
	inarg.fh = ff->fh;
	inarg.fsync_flags = datasync ? 1 : 0;
	req->in.h.opcode = FUSE_FSYNC;
	req->in.h.nodeid = get_node_id(inode);
	req->inode = inode;
	req->file = file;
	req->in.numargs = 1;
	req->in.args[0].size = sizeof(inarg);
	req->in.args[0].value = &inarg;
	request_send(fc, req);
	err = req->out.h.error;
	fuse_put_request(fc, req);
	if (err == -ENOSYS) {
		fc->no_fsync = 1;
		err = 0;
	}
	return err;
}

static ssize_t fuse_send_read(struct fuse_req *req, struct file *file,
			      struct inode *inode, loff_t pos,  size_t count)
{
	struct fuse_conn *fc = get_fuse_conn(inode);
	struct fuse_file *ff = file->private_data;
	struct fuse_read_in inarg;

	memset(&inarg, 0, sizeof(struct fuse_read_in));
	inarg.fh = ff->fh;
	inarg.offset = pos;
	inarg.size = count;
	req->in.h.opcode = FUSE_READ;
	req->in.h.nodeid = get_node_id(inode);
	req->inode = inode;
	req->file = file;
	req->in.numargs = 1;
	req->in.args[0].size = sizeof(struct fuse_read_in);
	req->in.args[0].value = &inarg;
	req->out.argpages = 1;
	req->out.argvar = 1;
	req->out.numargs = 1;
	req->out.args[0].size = count;
	request_send_nonint(fc, req);
	return req->out.args[0].size;
}

static int fuse_readpage(struct file *file, struct page *page)
{
	struct inode *inode = page->mapping->host;
	struct fuse_conn *fc = get_fuse_conn(inode);
	loff_t pos = (loff_t) page->index << PAGE_CACHE_SHIFT;
	struct fuse_req *req = fuse_get_request_nonint(fc);
	int err = -EINTR;
	if (!req)
		goto out;

	req->out.page_zeroing = 1;
	req->num_pages = 1;
	req->pages[0] = page;
	fuse_send_read(req, file, inode, pos, PAGE_CACHE_SIZE);
	err = req->out.h.error;
	fuse_put_request(fc, req);
	if (!err)
		SetPageUptodate(page);
 out:
	unlock_page(page);
	return err;
}

static ssize_t fuse_send_write(struct fuse_req *req, struct file *file,
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
	req->inode = inode;
	req->file = file;
	req->in.argpages = 1;
	req->in.numargs = 2;
	req->in.args[0].size = sizeof(struct fuse_write_in);
	req->in.args[0].value = &inarg;
	req->in.args[1].size = count;
	req->out.numargs = 1;
	req->out.args[0].size = sizeof(struct fuse_write_out);
	req->out.args[0].value = &outarg;
	request_send_nonint(fc, req);
	return outarg.size;
}

static int fuse_prepare_write(struct file *file, struct page *page,
			      unsigned offset, unsigned to)
{
	/* No op */
	return 0;
}

static int fuse_commit_write(struct file *file, struct page *page,
			     unsigned offset, unsigned to)
{
	int err;
	ssize_t nres;
	unsigned count = to - offset;
	struct inode *inode = page->mapping->host;
	struct fuse_conn *fc = get_fuse_conn(inode);
	loff_t pos = ((loff_t) page->index << PAGE_CACHE_SHIFT) + offset;
	struct fuse_req *req = fuse_get_request_nonint(fc);
	if (!req)
		return -EINTR;

	req->num_pages = 1;
	req->pages[0] = page;
	req->page_offset = offset;
	nres = fuse_send_write(req, file, inode, pos, count);
	err = req->out.h.error;
	fuse_put_request(fc, req);
	if (!err && nres != count)
		err = -EIO;
	if (!err) {
		pos += count;
		if (pos > i_size_read(inode))
			i_size_write(inode, pos);

		if (offset == 0 && to == PAGE_CACHE_SIZE) {
			clear_page_dirty(page);
			SetPageUptodate(page);
		}
	} else if (err == -EINTR || err == -EIO)
		fuse_invalidate_attr(inode);
	return err;
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

static struct file_operations fuse_file_operations = {
	.llseek		= generic_file_llseek,
	.read		= generic_file_read,
	.write		= generic_file_write,
	.mmap		= fuse_file_mmap,
	.open		= fuse_open,
	.flush		= fuse_flush,
	.release	= fuse_release,
	.fsync		= fuse_fsync,
	.sendfile	= generic_file_sendfile,
};

static struct address_space_operations fuse_file_aops  = {
	.readpage	= fuse_readpage,
	.prepare_write	= fuse_prepare_write,
	.commit_write	= fuse_commit_write,
	.set_page_dirty	= fuse_set_page_dirty,
};

void fuse_init_file_inode(struct inode *inode)
{
	inode->i_fop = &fuse_file_operations;
	inode->i_data.a_ops = &fuse_file_aops;
}
