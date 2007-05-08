/*
 *  linux/fs/fifo.c
 *
 *  written by Paul H. Hargrove
 *
 *  Fixes:
 *	10-06-1999, AV: fixed OOM handling in fifo_open(), moved
 *			initialization there, switched to external
 *			allocation of pipe_inode_info.
 */

#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/pipe_fs_i.h>

static void wait_for_partner(struct inode* inode, unsigned int *cnt)
{
	int cur = *cnt;	

	while (cur == *cnt) {
		pipe_wait(inode->i_pipe);
		if (signal_pending(current))
			break;
	}
}

static void wake_up_partner(struct inode* inode)
{
	wake_up_interruptible(&inode->i_pipe->wait);
}

static int fifo_open(struct inode *inode, struct file *filp)
{
	struct pipe_inode_info *pipe;
	int ret;

	mutex_lock(&inode->i_mutex);
	pipe = inode->i_pipe;
	if (!pipe) {
		ret = -ENOMEM;
		pipe = alloc_pipe_info(inode);
		if (!pipe)
			goto err_nocleanup;
		inode->i_pipe = pipe;
	}
	filp->f_version = 0;

	/* We can only do regular read/write on fifos */
	filp->f_mode &= (FMODE_READ | FMODE_WRITE);

	switch (filp->f_mode) {
	case 1:
	/*
	 *  O_RDONLY
	 *  POSIX.1 says that O_NONBLOCK means return with the FIFO
	 *  opened, even when there is no process writing the FIFO.
	 */
		filp->f_op = &read_fifo_fops;
		pipe->r_counter++;
		if (pipe->readers++ == 0)
			wake_up_partner(inode);

		if (!pipe->writers) {
			if ((filp->f_flags & O_NONBLOCK)) {
				/* suppress POLLHUP until we have
				 * seen a writer */
				filp->f_version = pipe->w_counter;
			} else 
			{
				wait_for_partner(inode, &pipe->w_counter);
				if(signal_pending(current))
					goto err_rd;
			}
		}
		break;
	
	case 2:
	/*
	 *  O_WRONLY
	 *  POSIX.1 says that O_NONBLOCK means return -1 with
	 *  errno=ENXIO when there is no process reading the FIFO.
	 */
		ret = -ENXIO;
		if ((filp->f_flags & O_NONBLOCK) && !pipe->readers)
			goto err;

		filp->f_op = &write_fifo_fops;
		pipe->w_counter++;
		if (!pipe->writers++)
			wake_up_partner(inode);

		if (!pipe->readers) {
			wait_for_partner(inode, &pipe->r_counter);
			if (signal_pending(current))
				goto err_wr;
		}
		break;
	
	case 3:
	/*
	 *  O_RDWR
	 *  POSIX.1 leaves this case "undefined" when O_NONBLOCK is set.
	 *  This implementation will NEVER block on a O_RDWR open, since
	 *  the process can at least talk to itself.
	 */
		filp->f_op = &rdwr_fifo_fops;

		pipe->readers++;
		pipe->writers++;
		pipe->r_counter++;
		pipe->w_counter++;
		if (pipe->readers == 1 || pipe->writers == 1)
			wake_up_partner(inode);
		break;

	default:
		ret = -EINVAL;
		goto err;
	}

	/* Ok! */
	mutex_unlock(&inode->i_mutex);
	return 0;

err_rd:
	if (!--pipe->readers)
		wake_up_interruptible(&pipe->wait);
	ret = -ERESTARTSYS;
	goto err;

err_wr:
	if (!--pipe->writers)
		wake_up_interruptible(&pipe->wait);
	ret = -ERESTARTSYS;
	goto err;

err:
	if (!pipe->readers && !pipe->writers)
		free_pipe_info(inode);

err_nocleanup:
	mutex_unlock(&inode->i_mutex);
	return ret;
}

/*
 * Dummy default file-operations: the only thing this does
 * is contain the open that then fills in the correct operations
 * depending on the access mode of the file...
 */
const struct file_operations def_fifo_fops = {
	.open		= fifo_open,	/* will set read or write pipe_fops */
};
