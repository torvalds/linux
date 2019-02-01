/* $Id: regops.c $ */
/** @file
 * vboxsf - VBox Linux Shared Folders VFS, regular file inode and file operations.
 */

/*
 * Copyright (C) 2006-2019 Oracle Corporation
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * Limitations: only COW memory mapping is supported
 */

#include "vfsmod.h"

static void *alloc_bounce_buffer(size_t * tmp_sizep, PRTCCPHYS physp, size_t
				 xfer_size, const char *caller)
{
	size_t tmp_size;
	void *tmp;

	/* try for big first. */
	tmp_size = RT_ALIGN_Z(xfer_size, PAGE_SIZE);
	if (tmp_size > 16U * _1K)
		tmp_size = 16U * _1K;
	tmp = kmalloc(tmp_size, GFP_KERNEL);
	if (!tmp) {
		/* fall back on a page sized buffer. */
		tmp = kmalloc(PAGE_SIZE, GFP_KERNEL);
		if (!tmp) {
			LogRel(("%s: could not allocate bounce buffer for xfer_size=%zu %s\n", caller, xfer_size));
			return NULL;
		}
		tmp_size = PAGE_SIZE;
	}

	*tmp_sizep = tmp_size;
	*physp = virt_to_phys(tmp);
	return tmp;
}

static void free_bounce_buffer(void *tmp)
{
	kfree(tmp);
}

/* fops */
static int sf_reg_read_aux(const char *caller, struct sf_glob_info *sf_g,
			   struct sf_reg_info *sf_r, void *buf,
			   uint32_t * nread, uint64_t pos)
{
    /** @todo bird: yes, kmap() and kmalloc() input only. Since the buffer is
     *        contiguous in physical memory (kmalloc or single page), we should
     *        use a physical address here to speed things up. */
	int rc = VbglR0SfRead(&client_handle, &sf_g->map, sf_r->handle,
			      pos, nread, buf, false /* already locked? */ );
	if (RT_FAILURE(rc)) {
		LogFunc(("VbglR0SfRead failed. caller=%s, rc=%Rrc\n", caller,
			 rc));
		return -EPROTO;
	}
	return 0;
}

static int sf_reg_write_aux(const char *caller, struct sf_glob_info *sf_g,
			    struct sf_reg_info *sf_r, void *buf,
			    uint32_t * nwritten, uint64_t pos)
{
    /** @todo bird: yes, kmap() and kmalloc() input only. Since the buffer is
     *        contiguous in physical memory (kmalloc or single page), we should
     *        use a physical address here to speed things up. */
	int rc = VbglR0SfWrite(&client_handle, &sf_g->map, sf_r->handle,
			       pos, nwritten, buf,
			       false /* already locked? */ );
	if (RT_FAILURE(rc)) {
		LogFunc(("VbglR0SfWrite failed. caller=%s, rc=%Rrc\n",
			 caller, rc));
		return -EPROTO;
	}
	return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 23) \
 && LINUX_VERSION_CODE <  KERNEL_VERSION(2, 6, 31)

void free_pipebuf(struct page *kpage)
{
	kunmap(kpage);
	__free_pages(kpage, 0);
}

void *sf_pipe_buf_map(struct pipe_inode_info *pipe,
		      struct pipe_buffer *pipe_buf, int atomic)
{
	return 0;
}

void sf_pipe_buf_get(struct pipe_inode_info *pipe, struct pipe_buffer *pipe_buf)
{
}

void sf_pipe_buf_unmap(struct pipe_inode_info *pipe,
		       struct pipe_buffer *pipe_buf, void *map_data)
{
}

int sf_pipe_buf_steal(struct pipe_inode_info *pipe,
		      struct pipe_buffer *pipe_buf)
{
	return 0;
}

static void sf_pipe_buf_release(struct pipe_inode_info *pipe,
				struct pipe_buffer *pipe_buf)
{
	free_pipebuf(pipe_buf->page);
}

int sf_pipe_buf_confirm(struct pipe_inode_info *info,
			struct pipe_buffer *pipe_buf)
{
	return 0;
}

static struct pipe_buf_operations sf_pipe_buf_ops = {
	.can_merge = 0,
	.map = sf_pipe_buf_map,
	.unmap = sf_pipe_buf_unmap,
	.confirm = sf_pipe_buf_confirm,
	.release = sf_pipe_buf_release,
	.steal = sf_pipe_buf_steal,
	.get = sf_pipe_buf_get,
};

#define LOCK_PIPE(pipe) \
    if (pipe->inode) \
        mutex_lock(&pipe->inode->i_mutex);

#define UNLOCK_PIPE(pipe) \
    if (pipe->inode) \
        mutex_unlock(&pipe->inode->i_mutex);

ssize_t
sf_splice_read(struct file *in, loff_t * poffset,
	       struct pipe_inode_info *pipe, size_t len, unsigned int flags)
{
	size_t bytes_remaining = len;
	loff_t orig_offset = *poffset;
	loff_t offset = orig_offset;
	struct inode *inode = GET_F_DENTRY(in)->d_inode;
	struct sf_glob_info *sf_g = GET_GLOB_INFO(inode->i_sb);
	struct sf_reg_info *sf_r = in->private_data;
	ssize_t retval;
	struct page *kpage = 0;
	size_t nsent = 0;

	TRACE();
	if (!S_ISREG(inode->i_mode)) {
		LogFunc(("read from non regular file %d\n", inode->i_mode));
		return -EINVAL;
	}
	if (!len) {
		return 0;
	}

	LOCK_PIPE(pipe);

	uint32_t req_size = 0;
	while (bytes_remaining > 0) {
		kpage = alloc_page(GFP_KERNEL);
		if (unlikely(kpage == NULL)) {
			UNLOCK_PIPE(pipe);
			return -ENOMEM;
		}
		req_size = 0;
		uint32_t nread = req_size =
		    (uint32_t) min(bytes_remaining, (size_t) PAGE_SIZE);
		uint32_t chunk = 0;
		void *kbuf = kmap(kpage);
		while (chunk < req_size) {
			retval =
			    sf_reg_read_aux(__func__, sf_g, sf_r, kbuf + chunk,
					    &nread, offset);
			if (retval < 0)
				goto err;
			if (nread == 0)
				break;
			chunk += nread;
			offset += nread;
			nread = req_size - chunk;
		}
		if (!pipe->readers) {
			send_sig(SIGPIPE, current, 0);
			retval = -EPIPE;
			goto err;
		}
		if (pipe->nrbufs < PIPE_BUFFERS) {
			struct pipe_buffer *pipebuf =
			    pipe->bufs +
			    ((pipe->curbuf + pipe->nrbufs) & (PIPE_BUFFERS -
							      1));
			pipebuf->page = kpage;
			pipebuf->ops = &sf_pipe_buf_ops;
			pipebuf->len = req_size;
			pipebuf->offset = 0;
			pipebuf->private = 0;
			pipebuf->flags = 0;
			pipe->nrbufs++;
			nsent += req_size;
			bytes_remaining -= req_size;
			if (signal_pending(current))
				break;
		} else {	/* pipe full */

			if (flags & SPLICE_F_NONBLOCK) {
				retval = -EAGAIN;
				goto err;
			}
			free_pipebuf(kpage);
			break;
		}
	}
	UNLOCK_PIPE(pipe);
	if (!nsent && signal_pending(current))
		return -ERESTARTSYS;
	*poffset += nsent;
	return offset - orig_offset;

 err:
	UNLOCK_PIPE(pipe);
	free_pipebuf(kpage);
	return retval;
}

#endif /* 2.6.23 <= LINUX_VERSION_CODE < 2.6.31 */

/**
 * Read from a regular file.
 *
 * @param file          the file
 * @param buf           the buffer
 * @param size          length of the buffer
 * @param off           offset within the file
 * @returns the number of read bytes on success, Linux error code otherwise
 */
static ssize_t sf_reg_read(struct file *file, char *buf, size_t size,
			   loff_t * off)
{
	int err;
	void *tmp;
	RTCCPHYS tmp_phys;
	size_t tmp_size;
	size_t left = size;
	ssize_t total_bytes_read = 0;
	struct inode *inode = GET_F_DENTRY(file)->d_inode;
	struct sf_glob_info *sf_g = GET_GLOB_INFO(inode->i_sb);
	struct sf_reg_info *sf_r = file->private_data;
	loff_t pos = *off;

	TRACE();
	if (!S_ISREG(inode->i_mode)) {
		LogFunc(("read from non regular file %d\n", inode->i_mode));
		return -EINVAL;
	}

	/** @todo XXX Check read permission according to inode->i_mode! */

	if (!size)
		return 0;

	tmp =
	    alloc_bounce_buffer(&tmp_size, &tmp_phys, size,
				__PRETTY_FUNCTION__);
	if (!tmp)
		return -ENOMEM;

	while (left) {
		uint32_t to_read, nread;

		to_read = tmp_size;
		if (to_read > left)
			to_read = (uint32_t) left;

		nread = to_read;

		err = sf_reg_read_aux(__func__, sf_g, sf_r, tmp, &nread, pos);
		if (err)
			goto fail;

		if (copy_to_user(buf, tmp, nread)) {
			err = -EFAULT;
			goto fail;
		}

		pos += nread;
		left -= nread;
		buf += nread;
		total_bytes_read += nread;
		if (nread != to_read)
			break;
	}

	*off += total_bytes_read;
	free_bounce_buffer(tmp);
	return total_bytes_read;

 fail:
	free_bounce_buffer(tmp);
	return err;
}

/**
 * Write to a regular file.
 *
 * @param file          the file
 * @param buf           the buffer
 * @param size          length of the buffer
 * @param off           offset within the file
 * @returns the number of written bytes on success, Linux error code otherwise
 */
static ssize_t sf_reg_write(struct file *file, const char *buf, size_t size,
			    loff_t * off)
{
	int err;
	void *tmp;
	RTCCPHYS tmp_phys;
	size_t tmp_size;
	size_t left = size;
	ssize_t total_bytes_written = 0;
	struct inode *inode = GET_F_DENTRY(file)->d_inode;
	struct sf_inode_info *sf_i = GET_INODE_INFO(inode);
	struct sf_glob_info *sf_g = GET_GLOB_INFO(inode->i_sb);
	struct sf_reg_info *sf_r = file->private_data;
	loff_t pos;

	TRACE();
	BUG_ON(!sf_i);
	BUG_ON(!sf_g);
	BUG_ON(!sf_r);

	if (!S_ISREG(inode->i_mode)) {
		LogFunc(("write to non regular file %d\n", inode->i_mode));
		return -EINVAL;
	}

	pos = *off;
	if (file->f_flags & O_APPEND) {
		pos = inode->i_size;
		*off = pos;
	}

	/** @todo XXX Check write permission according to inode->i_mode! */

	if (!size)
		return 0;

	tmp =
	    alloc_bounce_buffer(&tmp_size, &tmp_phys, size,
				__PRETTY_FUNCTION__);
	if (!tmp)
		return -ENOMEM;

	while (left) {
		uint32_t to_write, nwritten;

		to_write = tmp_size;
		if (to_write > left)
			to_write = (uint32_t) left;

		nwritten = to_write;

		if (copy_from_user(tmp, buf, to_write)) {
			err = -EFAULT;
			goto fail;
		}

		err =
		    VbglR0SfWritePhysCont(&client_handle, &sf_g->map,
					  sf_r->handle, pos, &nwritten,
					  tmp_phys);
		err = RT_FAILURE(err) ? -EPROTO : 0;
		if (err)
			goto fail;

		pos += nwritten;
		left -= nwritten;
		buf += nwritten;
		total_bytes_written += nwritten;
		if (nwritten != to_write)
			break;
	}

	*off += total_bytes_written;
	if (*off > inode->i_size)
		inode->i_size = *off;

	sf_i->force_restat = 1;
	free_bounce_buffer(tmp);
	return total_bytes_written;

 fail:
	free_bounce_buffer(tmp);
	return err;
}

/**
 * Open a regular file.
 *
 * @param inode         the inode
 * @param file          the file
 * @returns 0 on success, Linux error code otherwise
 */
static int sf_reg_open(struct inode *inode, struct file *file)
{
	int rc, rc_linux = 0;
	struct sf_glob_info *sf_g = GET_GLOB_INFO(inode->i_sb);
	struct sf_inode_info *sf_i = GET_INODE_INFO(inode);
	struct sf_reg_info *sf_r;
	SHFLCREATEPARMS params;

	TRACE();
	BUG_ON(!sf_g);
	BUG_ON(!sf_i);

	LogFunc(("open %s\n", sf_i->path->String.utf8));

	sf_r = kmalloc(sizeof(*sf_r), GFP_KERNEL);
	if (!sf_r) {
		LogRelFunc(("could not allocate reg info\n"));
		return -ENOMEM;
	}

	/* Already open? */
	if (sf_i->handle != SHFL_HANDLE_NIL) {
		/*
		 * This inode was created with sf_create_aux(). Check the CreateFlags:
		 * O_CREAT, O_TRUNC: inherent true (file was just created). Not sure
		 * about the access flags (SHFL_CF_ACCESS_*).
		 */
		sf_i->force_restat = 1;
		sf_r->handle = sf_i->handle;
		sf_i->handle = SHFL_HANDLE_NIL;
		sf_i->file = file;
		file->private_data = sf_r;
		return 0;
	}

	RT_ZERO(params);
	params.Handle = SHFL_HANDLE_NIL;
	/* We check the value of params.Handle afterwards to find out if
	 * the call succeeded or failed, as the API does not seem to cleanly
	 * distinguish error and informational messages.
	 *
	 * Furthermore, we must set params.Handle to SHFL_HANDLE_NIL to
	 * make the shared folders host service use our fMode parameter */

	if (file->f_flags & O_CREAT) {
		LogFunc(("O_CREAT set\n"));
		params.CreateFlags |= SHFL_CF_ACT_CREATE_IF_NEW;
		/* We ignore O_EXCL, as the Linux kernel seems to call create
		   beforehand itself, so O_EXCL should always fail. */
		if (file->f_flags & O_TRUNC) {
			LogFunc(("O_TRUNC set\n"));
			params.CreateFlags |= SHFL_CF_ACT_OVERWRITE_IF_EXISTS;
		} else
			params.CreateFlags |= SHFL_CF_ACT_OPEN_IF_EXISTS;
	} else {
		params.CreateFlags |= SHFL_CF_ACT_FAIL_IF_NEW;
		if (file->f_flags & O_TRUNC) {
			LogFunc(("O_TRUNC set\n"));
			params.CreateFlags |= SHFL_CF_ACT_OVERWRITE_IF_EXISTS;
		}
	}

	switch (file->f_flags & O_ACCMODE) {
	case O_RDONLY:
		params.CreateFlags |= SHFL_CF_ACCESS_READ;
		break;

	case O_WRONLY:
		params.CreateFlags |= SHFL_CF_ACCESS_WRITE;
		break;

	case O_RDWR:
		params.CreateFlags |= SHFL_CF_ACCESS_READWRITE;
		break;

	default:
		BUG();
	}

	if (file->f_flags & O_APPEND) {
		LogFunc(("O_APPEND set\n"));
		params.CreateFlags |= SHFL_CF_ACCESS_APPEND;
	}

	params.Info.Attr.fMode = inode->i_mode;
	LogFunc(("sf_reg_open: calling VbglR0SfCreate, file %s, flags=%#x, %#x\n", sf_i->path->String.utf8, file->f_flags, params.CreateFlags));
	rc = VbglR0SfCreate(&client_handle, &sf_g->map, sf_i->path, &params);
	if (RT_FAILURE(rc)) {
		LogFunc(("VbglR0SfCreate failed flags=%d,%#x rc=%Rrc\n",
			 file->f_flags, params.CreateFlags, rc));
		kfree(sf_r);
		return -RTErrConvertToErrno(rc);
	}

	if (SHFL_HANDLE_NIL == params.Handle) {
		switch (params.Result) {
		case SHFL_PATH_NOT_FOUND:
		case SHFL_FILE_NOT_FOUND:
			rc_linux = -ENOENT;
			break;
		case SHFL_FILE_EXISTS:
			rc_linux = -EEXIST;
			break;
		default:
			break;
		}
	}

	sf_i->force_restat = 1;
	sf_r->handle = params.Handle;
	sf_i->file = file;
	file->private_data = sf_r;
	return rc_linux;
}

/**
 * Close a regular file.
 *
 * @param inode         the inode
 * @param file          the file
 * @returns 0 on success, Linux error code otherwise
 */
static int sf_reg_release(struct inode *inode, struct file *file)
{
	int rc;
	struct sf_reg_info *sf_r;
	struct sf_glob_info *sf_g;
	struct sf_inode_info *sf_i = GET_INODE_INFO(inode);

	TRACE();
	sf_g = GET_GLOB_INFO(inode->i_sb);
	sf_r = file->private_data;

	BUG_ON(!sf_g);
	BUG_ON(!sf_r);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 4, 25)
	/* See the smbfs source (file.c). mmap in particular can cause data to be
	 * written to the file after it is closed, which we can't cope with.  We
	 * copy and paste the body of filemap_write_and_wait() here as it was not
	 * defined before 2.6.6 and not exported until quite a bit later. */
	/* filemap_write_and_wait(inode->i_mapping); */
	if (inode->i_mapping->nrpages
	    && filemap_fdatawrite(inode->i_mapping) != -EIO)
		filemap_fdatawait(inode->i_mapping);
#endif
	rc = VbglR0SfClose(&client_handle, &sf_g->map, sf_r->handle);
	if (RT_FAILURE(rc))
		LogFunc(("VbglR0SfClose failed rc=%Rrc\n", rc));

	kfree(sf_r);
	sf_i->file = NULL;
	sf_i->handle = SHFL_HANDLE_NIL;
	file->private_data = NULL;
	return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
static int sf_reg_fault(struct vm_fault *vmf)
#elif LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 25)
static int sf_reg_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0)
static struct page *sf_reg_nopage(struct vm_area_struct *vma,
				  unsigned long vaddr, int *type)
# define SET_TYPE(t) *type = (t)
#else  /* LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 0) */
static struct page *sf_reg_nopage(struct vm_area_struct *vma,
				  unsigned long vaddr, int unused)
# define SET_TYPE(t)
#endif
{
	struct page *page;
	char *buf;
	loff_t off;
	uint32_t nread = PAGE_SIZE;
	int err;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
	struct vm_area_struct *vma = vmf->vma;
#endif
	struct file *file = vma->vm_file;
	struct inode *inode = GET_F_DENTRY(file)->d_inode;
	struct sf_glob_info *sf_g = GET_GLOB_INFO(inode->i_sb);
	struct sf_reg_info *sf_r = file->private_data;

	TRACE();
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 25)
	if (vmf->pgoff > vma->vm_end)
		return VM_FAULT_SIGBUS;
#else
	if (vaddr > vma->vm_end) {
		SET_TYPE(VM_FAULT_SIGBUS);
		return NOPAGE_SIGBUS;
	}
#endif

	/* Don't use GFP_HIGHUSER as long as sf_reg_read_aux() calls VbglR0SfRead()
	 * which works on virtual addresses. On Linux cannot reliably determine the
	 * physical address for high memory, see rtR0MemObjNativeLockKernel(). */
	page = alloc_page(GFP_USER);
	if (!page) {
		LogRelFunc(("failed to allocate page\n"));
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 25)
		return VM_FAULT_OOM;
#else
		SET_TYPE(VM_FAULT_OOM);
		return NOPAGE_OOM;
#endif
	}

	buf = kmap(page);
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 25)
	off = (vmf->pgoff << PAGE_SHIFT);
#else
	off = (vaddr - vma->vm_start) + (vma->vm_pgoff << PAGE_SHIFT);
#endif
	err = sf_reg_read_aux(__func__, sf_g, sf_r, buf, &nread, off);
	if (err) {
		kunmap(page);
		put_page(page);
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 25)
		return VM_FAULT_SIGBUS;
#else
		SET_TYPE(VM_FAULT_SIGBUS);
		return NOPAGE_SIGBUS;
#endif
	}

	BUG_ON(nread > PAGE_SIZE);
	if (!nread) {
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 25)
		clear_user_page(page_address(page), vmf->pgoff, page);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0)
		clear_user_page(page_address(page), vaddr, page);
#else
		clear_user_page(page_address(page), vaddr);
#endif
	} else
		memset(buf + nread, 0, PAGE_SIZE - nread);

	flush_dcache_page(page);
	kunmap(page);
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 25)
	vmf->page = page;
	return 0;
#else
	SET_TYPE(VM_FAULT_MAJOR);
	return page;
#endif
}

static struct vm_operations_struct sf_vma_ops = {
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 25)
	.fault = sf_reg_fault
#else
	.nopage = sf_reg_nopage
#endif
};

static int sf_reg_mmap(struct file *file, struct vm_area_struct *vma)
{
	TRACE();
	if (vma->vm_flags & VM_SHARED) {
		LogFunc(("shared mmapping not available\n"));
		return -EINVAL;
	}

	vma->vm_ops = &sf_vma_ops;
	return 0;
}

struct file_operations sf_reg_fops = {
	.read = sf_reg_read,
	.open = sf_reg_open,
	.write = sf_reg_write,
	.release = sf_reg_release,
	.mmap = sf_reg_mmap,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0)
# if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 31)
/** @todo This code is known to cause caching of data which should not be
 * cached.  Investigate. */
# if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 23)
	.splice_read = sf_splice_read,
# else
	.sendfile = generic_file_sendfile,
# endif
	.aio_read = generic_file_aio_read,
	.aio_write = generic_file_aio_write,
# endif
# if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35)
	.fsync = noop_fsync,
# else
	.fsync = simple_sync_file,
# endif
	.llseek = generic_file_llseek,
#endif
};

struct inode_operations sf_reg_iops = {
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 0)
	.revalidate = sf_inode_revalidate
#else
	.getattr = sf_getattr,
	.setattr = sf_setattr
#endif
};

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0)

static int sf_readpage(struct file *file, struct page *page)
{
	struct inode *inode = GET_F_DENTRY(file)->d_inode;
	struct sf_glob_info *sf_g = GET_GLOB_INFO(inode->i_sb);
	struct sf_reg_info *sf_r = file->private_data;
	uint32_t nread = PAGE_SIZE;
	char *buf;
	loff_t off = ((loff_t) page->index) << PAGE_SHIFT;
	int ret;

	TRACE();

	buf = kmap(page);
	ret = sf_reg_read_aux(__func__, sf_g, sf_r, buf, &nread, off);
	if (ret) {
		kunmap(page);
		if (PageLocked(page))
			unlock_page(page);
		return ret;
	}
	BUG_ON(nread > PAGE_SIZE);
	memset(&buf[nread], 0, PAGE_SIZE - nread);
	flush_dcache_page(page);
	kunmap(page);
	SetPageUptodate(page);
	unlock_page(page);
	return 0;
}

static int sf_writepage(struct page *page, struct writeback_control *wbc)
{
	struct address_space *mapping = page->mapping;
	struct inode *inode = mapping->host;
	struct sf_glob_info *sf_g = GET_GLOB_INFO(inode->i_sb);
	struct sf_inode_info *sf_i = GET_INODE_INFO(inode);
	struct file *file = sf_i->file;
	struct sf_reg_info *sf_r = file->private_data;
	char *buf;
	uint32_t nwritten = PAGE_SIZE;
	int end_index = inode->i_size >> PAGE_SHIFT;
	loff_t off = ((loff_t) page->index) << PAGE_SHIFT;
	int err;

	TRACE();

	if (page->index >= end_index)
		nwritten = inode->i_size & (PAGE_SIZE - 1);

	buf = kmap(page);

	err = sf_reg_write_aux(__func__, sf_g, sf_r, buf, &nwritten, off);
	if (err < 0) {
		ClearPageUptodate(page);
		goto out;
	}

	if (off > inode->i_size)
		inode->i_size = off;

	if (PageError(page))
		ClearPageError(page);
	err = 0;

 out:
	kunmap(page);

	unlock_page(page);
	return err;
}

# if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24)

int sf_write_begin(struct file *file, struct address_space *mapping, loff_t pos,
		   unsigned len, unsigned flags, struct page **pagep,
		   void **fsdata)
{
	TRACE();

	return simple_write_begin(file, mapping, pos, len, flags, pagep,
				  fsdata);
}

int sf_write_end(struct file *file, struct address_space *mapping, loff_t pos,
		 unsigned len, unsigned copied, struct page *page, void *fsdata)
{
	struct inode *inode = mapping->host;
	struct sf_glob_info *sf_g = GET_GLOB_INFO(inode->i_sb);
	struct sf_reg_info *sf_r = file->private_data;
	void *buf;
	unsigned from = pos & (PAGE_SIZE - 1);
	uint32_t nwritten = len;
	int err;

	TRACE();

	buf = kmap(page);
	err =
	    sf_reg_write_aux(__func__, sf_g, sf_r, buf + from, &nwritten, pos);
	kunmap(page);

	if (err >= 0) {
		if (!PageUptodate(page) && nwritten == PAGE_SIZE)
			SetPageUptodate(page);

		pos += nwritten;
		if (pos > inode->i_size)
			inode->i_size = pos;
	}

	unlock_page(page);
#  if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 6, 0)
	put_page(page);
#  else
	page_cache_release(page);
#  endif

	return nwritten;
}

# endif	/* KERNEL_VERSION >= 2.6.24 */

struct address_space_operations sf_reg_aops = {
	.readpage = sf_readpage,
	.writepage = sf_writepage,
# if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24)
	.write_begin = sf_write_begin,
	.write_end = sf_write_end,
# else
	.prepare_write = simple_prepare_write,
	.commit_write = simple_commit_write,
# endif
};

#endif /* LINUX_VERSION_CODE >= 2.6.0 */

