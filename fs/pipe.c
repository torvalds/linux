/*
 *  linux/fs/pipe.c
 *
 *  Copyright (C) 1991, 1992, 1999  Linus Torvalds
 */

#include <linux/mm.h>
#include <linux/file.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/pipe_fs_i.h>
#include <linux/uio.h>
#include <linux/highmem.h>

#include <asm/uaccess.h>
#include <asm/ioctls.h>

/*
 * We use a start+len construction, which provides full use of the 
 * allocated memory.
 * -- Florian Coosmann (FGC)
 * 
 * Reads with count = 0 should always return 0.
 * -- Julian Bradfield 1999-06-07.
 *
 * FIFOs and Pipes now generate SIGIO for both readers and writers.
 * -- Jeremy Elson <jelson@circlemud.org> 2001-08-16
 *
 * pipe_read & write cleanup
 * -- Manfred Spraul <manfred@colorfullife.com> 2002-05-09
 */

/* Drop the inode semaphore and wait for a pipe event, atomically */
void pipe_wait(struct inode * inode)
{
	DEFINE_WAIT(wait);

	/*
	 * Pipes are system-local resources, so sleeping on them
	 * is considered a noninteractive wait:
	 */
	prepare_to_wait(PIPE_WAIT(*inode), &wait, TASK_INTERRUPTIBLE|TASK_NONINTERACTIVE);
	mutex_unlock(PIPE_MUTEX(*inode));
	schedule();
	finish_wait(PIPE_WAIT(*inode), &wait);
	mutex_lock(PIPE_MUTEX(*inode));
}

static int
pipe_iov_copy_from_user(void *to, struct iovec *iov, unsigned long len)
{
	unsigned long copy;

	while (len > 0) {
		while (!iov->iov_len)
			iov++;
		copy = min_t(unsigned long, len, iov->iov_len);

		if (copy_from_user(to, iov->iov_base, copy))
			return -EFAULT;
		to += copy;
		len -= copy;
		iov->iov_base += copy;
		iov->iov_len -= copy;
	}
	return 0;
}

static int
pipe_iov_copy_to_user(struct iovec *iov, const void *from, unsigned long len)
{
	unsigned long copy;

	while (len > 0) {
		while (!iov->iov_len)
			iov++;
		copy = min_t(unsigned long, len, iov->iov_len);

		if (copy_to_user(iov->iov_base, from, copy))
			return -EFAULT;
		from += copy;
		len -= copy;
		iov->iov_base += copy;
		iov->iov_len -= copy;
	}
	return 0;
}

static void anon_pipe_buf_release(struct pipe_inode_info *info, struct pipe_buffer *buf)
{
	struct page *page = buf->page;

	if (info->tmp_page) {
		__free_page(page);
		return;
	}
	info->tmp_page = page;
}

static void *anon_pipe_buf_map(struct file *file, struct pipe_inode_info *info, struct pipe_buffer *buf)
{
	return kmap(buf->page);
}

static void anon_pipe_buf_unmap(struct pipe_inode_info *info, struct pipe_buffer *buf)
{
	kunmap(buf->page);
}

static struct pipe_buf_operations anon_pipe_buf_ops = {
	.can_merge = 1,
	.map = anon_pipe_buf_map,
	.unmap = anon_pipe_buf_unmap,
	.release = anon_pipe_buf_release,
};

static ssize_t
pipe_readv(struct file *filp, const struct iovec *_iov,
	   unsigned long nr_segs, loff_t *ppos)
{
	struct inode *inode = filp->f_dentry->d_inode;
	struct pipe_inode_info *info;
	int do_wakeup;
	ssize_t ret;
	struct iovec *iov = (struct iovec *)_iov;
	size_t total_len;

	total_len = iov_length(iov, nr_segs);
	/* Null read succeeds. */
	if (unlikely(total_len == 0))
		return 0;

	do_wakeup = 0;
	ret = 0;
	mutex_lock(PIPE_MUTEX(*inode));
	info = inode->i_pipe;
	for (;;) {
		int bufs = info->nrbufs;
		if (bufs) {
			int curbuf = info->curbuf;
			struct pipe_buffer *buf = info->bufs + curbuf;
			struct pipe_buf_operations *ops = buf->ops;
			void *addr;
			size_t chars = buf->len;
			int error;

			if (chars > total_len)
				chars = total_len;

			addr = ops->map(filp, info, buf);
			error = pipe_iov_copy_to_user(iov, addr + buf->offset, chars);
			ops->unmap(info, buf);
			if (unlikely(error)) {
				if (!ret) ret = -EFAULT;
				break;
			}
			ret += chars;
			buf->offset += chars;
			buf->len -= chars;
			if (!buf->len) {
				buf->ops = NULL;
				ops->release(info, buf);
				curbuf = (curbuf + 1) & (PIPE_BUFFERS-1);
				info->curbuf = curbuf;
				info->nrbufs = --bufs;
				do_wakeup = 1;
			}
			total_len -= chars;
			if (!total_len)
				break;	/* common path: read succeeded */
		}
		if (bufs)	/* More to do? */
			continue;
		if (!PIPE_WRITERS(*inode))
			break;
		if (!PIPE_WAITING_WRITERS(*inode)) {
			/* syscall merging: Usually we must not sleep
			 * if O_NONBLOCK is set, or if we got some data.
			 * But if a writer sleeps in kernel space, then
			 * we can wait for that data without violating POSIX.
			 */
			if (ret)
				break;
			if (filp->f_flags & O_NONBLOCK) {
				ret = -EAGAIN;
				break;
			}
		}
		if (signal_pending(current)) {
			if (!ret) ret = -ERESTARTSYS;
			break;
		}
		if (do_wakeup) {
			wake_up_interruptible_sync(PIPE_WAIT(*inode));
 			kill_fasync(PIPE_FASYNC_WRITERS(*inode), SIGIO, POLL_OUT);
		}
		pipe_wait(inode);
	}
	mutex_unlock(PIPE_MUTEX(*inode));
	/* Signal writers asynchronously that there is more room.  */
	if (do_wakeup) {
		wake_up_interruptible(PIPE_WAIT(*inode));
		kill_fasync(PIPE_FASYNC_WRITERS(*inode), SIGIO, POLL_OUT);
	}
	if (ret > 0)
		file_accessed(filp);
	return ret;
}

static ssize_t
pipe_read(struct file *filp, char __user *buf, size_t count, loff_t *ppos)
{
	struct iovec iov = { .iov_base = buf, .iov_len = count };
	return pipe_readv(filp, &iov, 1, ppos);
}

static ssize_t
pipe_writev(struct file *filp, const struct iovec *_iov,
	    unsigned long nr_segs, loff_t *ppos)
{
	struct inode *inode = filp->f_dentry->d_inode;
	struct pipe_inode_info *info;
	ssize_t ret;
	int do_wakeup;
	struct iovec *iov = (struct iovec *)_iov;
	size_t total_len;
	ssize_t chars;

	total_len = iov_length(iov, nr_segs);
	/* Null write succeeds. */
	if (unlikely(total_len == 0))
		return 0;

	do_wakeup = 0;
	ret = 0;
	mutex_lock(PIPE_MUTEX(*inode));
	info = inode->i_pipe;

	if (!PIPE_READERS(*inode)) {
		send_sig(SIGPIPE, current, 0);
		ret = -EPIPE;
		goto out;
	}

	/* We try to merge small writes */
	chars = total_len & (PAGE_SIZE-1); /* size of the last buffer */
	if (info->nrbufs && chars != 0) {
		int lastbuf = (info->curbuf + info->nrbufs - 1) & (PIPE_BUFFERS-1);
		struct pipe_buffer *buf = info->bufs + lastbuf;
		struct pipe_buf_operations *ops = buf->ops;
		int offset = buf->offset + buf->len;
		if (ops->can_merge && offset + chars <= PAGE_SIZE) {
			void *addr = ops->map(filp, info, buf);
			int error = pipe_iov_copy_from_user(offset + addr, iov, chars);
			ops->unmap(info, buf);
			ret = error;
			do_wakeup = 1;
			if (error)
				goto out;
			buf->len += chars;
			total_len -= chars;
			ret = chars;
			if (!total_len)
				goto out;
		}
	}

	for (;;) {
		int bufs;
		if (!PIPE_READERS(*inode)) {
			send_sig(SIGPIPE, current, 0);
			if (!ret) ret = -EPIPE;
			break;
		}
		bufs = info->nrbufs;
		if (bufs < PIPE_BUFFERS) {
			int newbuf = (info->curbuf + bufs) & (PIPE_BUFFERS-1);
			struct pipe_buffer *buf = info->bufs + newbuf;
			struct page *page = info->tmp_page;
			int error;

			if (!page) {
				page = alloc_page(GFP_HIGHUSER);
				if (unlikely(!page)) {
					ret = ret ? : -ENOMEM;
					break;
				}
				info->tmp_page = page;
			}
			/* Always wakeup, even if the copy fails. Otherwise
			 * we lock up (O_NONBLOCK-)readers that sleep due to
			 * syscall merging.
			 * FIXME! Is this really true?
			 */
			do_wakeup = 1;
			chars = PAGE_SIZE;
			if (chars > total_len)
				chars = total_len;

			error = pipe_iov_copy_from_user(kmap(page), iov, chars);
			kunmap(page);
			if (unlikely(error)) {
				if (!ret) ret = -EFAULT;
				break;
			}
			ret += chars;

			/* Insert it into the buffer array */
			buf->page = page;
			buf->ops = &anon_pipe_buf_ops;
			buf->offset = 0;
			buf->len = chars;
			info->nrbufs = ++bufs;
			info->tmp_page = NULL;

			total_len -= chars;
			if (!total_len)
				break;
		}
		if (bufs < PIPE_BUFFERS)
			continue;
		if (filp->f_flags & O_NONBLOCK) {
			if (!ret) ret = -EAGAIN;
			break;
		}
		if (signal_pending(current)) {
			if (!ret) ret = -ERESTARTSYS;
			break;
		}
		if (do_wakeup) {
			wake_up_interruptible_sync(PIPE_WAIT(*inode));
			kill_fasync(PIPE_FASYNC_READERS(*inode), SIGIO, POLL_IN);
			do_wakeup = 0;
		}
		PIPE_WAITING_WRITERS(*inode)++;
		pipe_wait(inode);
		PIPE_WAITING_WRITERS(*inode)--;
	}
out:
	mutex_unlock(PIPE_MUTEX(*inode));
	if (do_wakeup) {
		wake_up_interruptible(PIPE_WAIT(*inode));
		kill_fasync(PIPE_FASYNC_READERS(*inode), SIGIO, POLL_IN);
	}
	if (ret > 0)
		file_update_time(filp);
	return ret;
}

static ssize_t
pipe_write(struct file *filp, const char __user *buf,
	   size_t count, loff_t *ppos)
{
	struct iovec iov = { .iov_base = (void __user *)buf, .iov_len = count };
	return pipe_writev(filp, &iov, 1, ppos);
}

static ssize_t
bad_pipe_r(struct file *filp, char __user *buf, size_t count, loff_t *ppos)
{
	return -EBADF;
}

static ssize_t
bad_pipe_w(struct file *filp, const char __user *buf, size_t count, loff_t *ppos)
{
	return -EBADF;
}

static int
pipe_ioctl(struct inode *pino, struct file *filp,
	   unsigned int cmd, unsigned long arg)
{
	struct inode *inode = filp->f_dentry->d_inode;
	struct pipe_inode_info *info;
	int count, buf, nrbufs;

	switch (cmd) {
		case FIONREAD:
			mutex_lock(PIPE_MUTEX(*inode));
			info =  inode->i_pipe;
			count = 0;
			buf = info->curbuf;
			nrbufs = info->nrbufs;
			while (--nrbufs >= 0) {
				count += info->bufs[buf].len;
				buf = (buf+1) & (PIPE_BUFFERS-1);
			}
			mutex_unlock(PIPE_MUTEX(*inode));
			return put_user(count, (int __user *)arg);
		default:
			return -EINVAL;
	}
}

/* No kernel lock held - fine */
static unsigned int
pipe_poll(struct file *filp, poll_table *wait)
{
	unsigned int mask;
	struct inode *inode = filp->f_dentry->d_inode;
	struct pipe_inode_info *info = inode->i_pipe;
	int nrbufs;

	poll_wait(filp, PIPE_WAIT(*inode), wait);

	/* Reading only -- no need for acquiring the semaphore.  */
	nrbufs = info->nrbufs;
	mask = 0;
	if (filp->f_mode & FMODE_READ) {
		mask = (nrbufs > 0) ? POLLIN | POLLRDNORM : 0;
		if (!PIPE_WRITERS(*inode) && filp->f_version != PIPE_WCOUNTER(*inode))
			mask |= POLLHUP;
	}

	if (filp->f_mode & FMODE_WRITE) {
		mask |= (nrbufs < PIPE_BUFFERS) ? POLLOUT | POLLWRNORM : 0;
		/*
		 * Most Unices do not set POLLERR for FIFOs but on Linux they
		 * behave exactly like pipes for poll().
		 */
		if (!PIPE_READERS(*inode))
			mask |= POLLERR;
	}

	return mask;
}

static int
pipe_release(struct inode *inode, int decr, int decw)
{
	mutex_lock(PIPE_MUTEX(*inode));
	PIPE_READERS(*inode) -= decr;
	PIPE_WRITERS(*inode) -= decw;
	if (!PIPE_READERS(*inode) && !PIPE_WRITERS(*inode)) {
		free_pipe_info(inode);
	} else {
		wake_up_interruptible(PIPE_WAIT(*inode));
		kill_fasync(PIPE_FASYNC_READERS(*inode), SIGIO, POLL_IN);
		kill_fasync(PIPE_FASYNC_WRITERS(*inode), SIGIO, POLL_OUT);
	}
	mutex_unlock(PIPE_MUTEX(*inode));

	return 0;
}

static int
pipe_read_fasync(int fd, struct file *filp, int on)
{
	struct inode *inode = filp->f_dentry->d_inode;
	int retval;

	mutex_lock(PIPE_MUTEX(*inode));
	retval = fasync_helper(fd, filp, on, PIPE_FASYNC_READERS(*inode));
	mutex_unlock(PIPE_MUTEX(*inode));

	if (retval < 0)
		return retval;

	return 0;
}


static int
pipe_write_fasync(int fd, struct file *filp, int on)
{
	struct inode *inode = filp->f_dentry->d_inode;
	int retval;

	mutex_lock(PIPE_MUTEX(*inode));
	retval = fasync_helper(fd, filp, on, PIPE_FASYNC_WRITERS(*inode));
	mutex_unlock(PIPE_MUTEX(*inode));

	if (retval < 0)
		return retval;

	return 0;
}


static int
pipe_rdwr_fasync(int fd, struct file *filp, int on)
{
	struct inode *inode = filp->f_dentry->d_inode;
	int retval;

	mutex_lock(PIPE_MUTEX(*inode));

	retval = fasync_helper(fd, filp, on, PIPE_FASYNC_READERS(*inode));

	if (retval >= 0)
		retval = fasync_helper(fd, filp, on, PIPE_FASYNC_WRITERS(*inode));

	mutex_unlock(PIPE_MUTEX(*inode));

	if (retval < 0)
		return retval;

	return 0;
}


static int
pipe_read_release(struct inode *inode, struct file *filp)
{
	pipe_read_fasync(-1, filp, 0);
	return pipe_release(inode, 1, 0);
}

static int
pipe_write_release(struct inode *inode, struct file *filp)
{
	pipe_write_fasync(-1, filp, 0);
	return pipe_release(inode, 0, 1);
}

static int
pipe_rdwr_release(struct inode *inode, struct file *filp)
{
	int decr, decw;

	pipe_rdwr_fasync(-1, filp, 0);
	decr = (filp->f_mode & FMODE_READ) != 0;
	decw = (filp->f_mode & FMODE_WRITE) != 0;
	return pipe_release(inode, decr, decw);
}

static int
pipe_read_open(struct inode *inode, struct file *filp)
{
	/* We could have perhaps used atomic_t, but this and friends
	   below are the only places.  So it doesn't seem worthwhile.  */
	mutex_lock(PIPE_MUTEX(*inode));
	PIPE_READERS(*inode)++;
	mutex_unlock(PIPE_MUTEX(*inode));

	return 0;
}

static int
pipe_write_open(struct inode *inode, struct file *filp)
{
	mutex_lock(PIPE_MUTEX(*inode));
	PIPE_WRITERS(*inode)++;
	mutex_unlock(PIPE_MUTEX(*inode));

	return 0;
}

static int
pipe_rdwr_open(struct inode *inode, struct file *filp)
{
	mutex_lock(PIPE_MUTEX(*inode));
	if (filp->f_mode & FMODE_READ)
		PIPE_READERS(*inode)++;
	if (filp->f_mode & FMODE_WRITE)
		PIPE_WRITERS(*inode)++;
	mutex_unlock(PIPE_MUTEX(*inode));

	return 0;
}

/*
 * The file_operations structs are not static because they
 * are also used in linux/fs/fifo.c to do operations on FIFOs.
 */
struct file_operations read_fifo_fops = {
	.llseek		= no_llseek,
	.read		= pipe_read,
	.readv		= pipe_readv,
	.write		= bad_pipe_w,
	.poll		= pipe_poll,
	.ioctl		= pipe_ioctl,
	.open		= pipe_read_open,
	.release	= pipe_read_release,
	.fasync		= pipe_read_fasync,
};

struct file_operations write_fifo_fops = {
	.llseek		= no_llseek,
	.read		= bad_pipe_r,
	.write		= pipe_write,
	.writev		= pipe_writev,
	.poll		= pipe_poll,
	.ioctl		= pipe_ioctl,
	.open		= pipe_write_open,
	.release	= pipe_write_release,
	.fasync		= pipe_write_fasync,
};

struct file_operations rdwr_fifo_fops = {
	.llseek		= no_llseek,
	.read		= pipe_read,
	.readv		= pipe_readv,
	.write		= pipe_write,
	.writev		= pipe_writev,
	.poll		= pipe_poll,
	.ioctl		= pipe_ioctl,
	.open		= pipe_rdwr_open,
	.release	= pipe_rdwr_release,
	.fasync		= pipe_rdwr_fasync,
};

struct file_operations read_pipe_fops = {
	.llseek		= no_llseek,
	.read		= pipe_read,
	.readv		= pipe_readv,
	.write		= bad_pipe_w,
	.poll		= pipe_poll,
	.ioctl		= pipe_ioctl,
	.open		= pipe_read_open,
	.release	= pipe_read_release,
	.fasync		= pipe_read_fasync,
};

struct file_operations write_pipe_fops = {
	.llseek		= no_llseek,
	.read		= bad_pipe_r,
	.write		= pipe_write,
	.writev		= pipe_writev,
	.poll		= pipe_poll,
	.ioctl		= pipe_ioctl,
	.open		= pipe_write_open,
	.release	= pipe_write_release,
	.fasync		= pipe_write_fasync,
};

struct file_operations rdwr_pipe_fops = {
	.llseek		= no_llseek,
	.read		= pipe_read,
	.readv		= pipe_readv,
	.write		= pipe_write,
	.writev		= pipe_writev,
	.poll		= pipe_poll,
	.ioctl		= pipe_ioctl,
	.open		= pipe_rdwr_open,
	.release	= pipe_rdwr_release,
	.fasync		= pipe_rdwr_fasync,
};

void free_pipe_info(struct inode *inode)
{
	int i;
	struct pipe_inode_info *info = inode->i_pipe;

	inode->i_pipe = NULL;
	for (i = 0; i < PIPE_BUFFERS; i++) {
		struct pipe_buffer *buf = info->bufs + i;
		if (buf->ops)
			buf->ops->release(info, buf);
	}
	if (info->tmp_page)
		__free_page(info->tmp_page);
	kfree(info);
}

struct inode* pipe_new(struct inode* inode)
{
	struct pipe_inode_info *info;

	info = kmalloc(sizeof(struct pipe_inode_info), GFP_KERNEL);
	if (!info)
		goto fail_page;
	memset(info, 0, sizeof(*info));
	inode->i_pipe = info;

	init_waitqueue_head(PIPE_WAIT(*inode));
	PIPE_RCOUNTER(*inode) = PIPE_WCOUNTER(*inode) = 1;

	return inode;
fail_page:
	return NULL;
}

static struct vfsmount *pipe_mnt;
static int pipefs_delete_dentry(struct dentry *dentry)
{
	return 1;
}
static struct dentry_operations pipefs_dentry_operations = {
	.d_delete	= pipefs_delete_dentry,
};

static struct inode * get_pipe_inode(void)
{
	struct inode *inode = new_inode(pipe_mnt->mnt_sb);

	if (!inode)
		goto fail_inode;

	if(!pipe_new(inode))
		goto fail_iput;
	PIPE_READERS(*inode) = PIPE_WRITERS(*inode) = 1;
	inode->i_fop = &rdwr_pipe_fops;

	/*
	 * Mark the inode dirty from the very beginning,
	 * that way it will never be moved to the dirty
	 * list because "mark_inode_dirty()" will think
	 * that it already _is_ on the dirty list.
	 */
	inode->i_state = I_DIRTY;
	inode->i_mode = S_IFIFO | S_IRUSR | S_IWUSR;
	inode->i_uid = current->fsuid;
	inode->i_gid = current->fsgid;
	inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	inode->i_blksize = PAGE_SIZE;
	return inode;

fail_iput:
	iput(inode);
fail_inode:
	return NULL;
}

int do_pipe(int *fd)
{
	struct qstr this;
	char name[32];
	struct dentry *dentry;
	struct inode * inode;
	struct file *f1, *f2;
	int error;
	int i,j;

	error = -ENFILE;
	f1 = get_empty_filp();
	if (!f1)
		goto no_files;

	f2 = get_empty_filp();
	if (!f2)
		goto close_f1;

	inode = get_pipe_inode();
	if (!inode)
		goto close_f12;

	error = get_unused_fd();
	if (error < 0)
		goto close_f12_inode;
	i = error;

	error = get_unused_fd();
	if (error < 0)
		goto close_f12_inode_i;
	j = error;

	error = -ENOMEM;
	sprintf(name, "[%lu]", inode->i_ino);
	this.name = name;
	this.len = strlen(name);
	this.hash = inode->i_ino; /* will go */
	dentry = d_alloc(pipe_mnt->mnt_sb->s_root, &this);
	if (!dentry)
		goto close_f12_inode_i_j;
	dentry->d_op = &pipefs_dentry_operations;
	d_add(dentry, inode);
	f1->f_vfsmnt = f2->f_vfsmnt = mntget(mntget(pipe_mnt));
	f1->f_dentry = f2->f_dentry = dget(dentry);
	f1->f_mapping = f2->f_mapping = inode->i_mapping;

	/* read file */
	f1->f_pos = f2->f_pos = 0;
	f1->f_flags = O_RDONLY;
	f1->f_op = &read_pipe_fops;
	f1->f_mode = FMODE_READ;
	f1->f_version = 0;

	/* write file */
	f2->f_flags = O_WRONLY;
	f2->f_op = &write_pipe_fops;
	f2->f_mode = FMODE_WRITE;
	f2->f_version = 0;

	fd_install(i, f1);
	fd_install(j, f2);
	fd[0] = i;
	fd[1] = j;
	return 0;

close_f12_inode_i_j:
	put_unused_fd(j);
close_f12_inode_i:
	put_unused_fd(i);
close_f12_inode:
	free_pipe_info(inode);
	iput(inode);
close_f12:
	put_filp(f2);
close_f1:
	put_filp(f1);
no_files:
	return error;	
}

/*
 * pipefs should _never_ be mounted by userland - too much of security hassle,
 * no real gain from having the whole whorehouse mounted. So we don't need
 * any operations on the root directory. However, we need a non-trivial
 * d_name - pipe: will go nicely and kill the special-casing in procfs.
 */

static struct super_block *pipefs_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return get_sb_pseudo(fs_type, "pipe:", NULL, PIPEFS_MAGIC);
}

static struct file_system_type pipe_fs_type = {
	.name		= "pipefs",
	.get_sb		= pipefs_get_sb,
	.kill_sb	= kill_anon_super,
};

static int __init init_pipe_fs(void)
{
	int err = register_filesystem(&pipe_fs_type);
	if (!err) {
		pipe_mnt = kern_mount(&pipe_fs_type);
		if (IS_ERR(pipe_mnt)) {
			err = PTR_ERR(pipe_mnt);
			unregister_filesystem(&pipe_fs_type);
		}
	}
	return err;
}

static void __exit exit_pipe_fs(void)
{
	unregister_filesystem(&pipe_fs_type);
	mntput(pipe_mnt);
}

fs_initcall(init_pipe_fs);
module_exit(exit_pipe_fs);
