// SPDX-License-Identifier: GPL-2.0
/*
 * I/O and data path helper functionality.
 *
 * Borrowed from NFS Copyright (c) 2016 Trond Myklebust
 */

#include <linux/kernel.h>
#include <linux/netfs.h>
#include "internal.h"

/*
 * inode_dio_wait_interruptible - wait for outstanding DIO requests to finish
 * @inode: inode to wait for
 *
 * Waits for all pending direct I/O requests to finish so that we can
 * proceed with a truncate or equivalent operation.
 *
 * Must be called under a lock that serializes taking new references
 * to i_dio_count, usually by inode->i_mutex.
 */
static int inode_dio_wait_interruptible(struct inode *inode)
{
	if (!atomic_read(&inode->i_dio_count))
		return 0;

	wait_queue_head_t *wq = bit_waitqueue(&inode->i_state, __I_DIO_WAKEUP);
	DEFINE_WAIT_BIT(q, &inode->i_state, __I_DIO_WAKEUP);

	for (;;) {
		prepare_to_wait(wq, &q.wq_entry, TASK_INTERRUPTIBLE);
		if (!atomic_read(&inode->i_dio_count))
			break;
		if (signal_pending(current))
			break;
		schedule();
	}
	finish_wait(wq, &q.wq_entry);

	return atomic_read(&inode->i_dio_count) ? -ERESTARTSYS : 0;
}

/* Call with exclusively locked inode->i_rwsem */
static int netfs_block_o_direct(struct netfs_inode *ictx)
{
	if (!test_bit(NETFS_ICTX_ODIRECT, &ictx->flags))
		return 0;
	clear_bit(NETFS_ICTX_ODIRECT, &ictx->flags);
	return inode_dio_wait_interruptible(&ictx->inode);
}

/**
 * netfs_start_io_read - declare the file is being used for buffered reads
 * @inode: file inode
 *
 * Declare that a buffered read operation is about to start, and ensure
 * that we block all direct I/O.
 * On exit, the function ensures that the NETFS_ICTX_ODIRECT flag is unset,
 * and holds a shared lock on inode->i_rwsem to ensure that the flag
 * cannot be changed.
 * In practice, this means that buffered read operations are allowed to
 * execute in parallel, thanks to the shared lock, whereas direct I/O
 * operations need to wait to grab an exclusive lock in order to set
 * NETFS_ICTX_ODIRECT.
 * Note that buffered writes and truncates both take a write lock on
 * inode->i_rwsem, meaning that those are serialised w.r.t. the reads.
 */
int netfs_start_io_read(struct inode *inode)
	__acquires(inode->i_rwsem)
{
	struct netfs_inode *ictx = netfs_inode(inode);

	/* Be an optimist! */
	if (down_read_interruptible(&inode->i_rwsem) < 0)
		return -ERESTARTSYS;
	if (test_bit(NETFS_ICTX_ODIRECT, &ictx->flags) == 0)
		return 0;
	up_read(&inode->i_rwsem);

	/* Slow path.... */
	if (down_write_killable(&inode->i_rwsem) < 0)
		return -ERESTARTSYS;
	if (netfs_block_o_direct(ictx) < 0) {
		up_write(&inode->i_rwsem);
		return -ERESTARTSYS;
	}
	downgrade_write(&inode->i_rwsem);
	return 0;
}
EXPORT_SYMBOL(netfs_start_io_read);

/**
 * netfs_end_io_read - declare that the buffered read operation is done
 * @inode: file inode
 *
 * Declare that a buffered read operation is done, and release the shared
 * lock on inode->i_rwsem.
 */
void netfs_end_io_read(struct inode *inode)
	__releases(inode->i_rwsem)
{
	up_read(&inode->i_rwsem);
}
EXPORT_SYMBOL(netfs_end_io_read);

/**
 * netfs_start_io_write - declare the file is being used for buffered writes
 * @inode: file inode
 *
 * Declare that a buffered read operation is about to start, and ensure
 * that we block all direct I/O.
 */
int netfs_start_io_write(struct inode *inode)
	__acquires(inode->i_rwsem)
{
	struct netfs_inode *ictx = netfs_inode(inode);

	if (down_write_killable(&inode->i_rwsem) < 0)
		return -ERESTARTSYS;
	if (netfs_block_o_direct(ictx) < 0) {
		up_write(&inode->i_rwsem);
		return -ERESTARTSYS;
	}
	return 0;
}
EXPORT_SYMBOL(netfs_start_io_write);

/**
 * netfs_end_io_write - declare that the buffered write operation is done
 * @inode: file inode
 *
 * Declare that a buffered write operation is done, and release the
 * lock on inode->i_rwsem.
 */
void netfs_end_io_write(struct inode *inode)
	__releases(inode->i_rwsem)
{
	up_write(&inode->i_rwsem);
}
EXPORT_SYMBOL(netfs_end_io_write);

/* Call with exclusively locked inode->i_rwsem */
static int netfs_block_buffered(struct inode *inode)
{
	struct netfs_inode *ictx = netfs_inode(inode);
	int ret;

	if (!test_bit(NETFS_ICTX_ODIRECT, &ictx->flags)) {
		set_bit(NETFS_ICTX_ODIRECT, &ictx->flags);
		if (inode->i_mapping->nrpages != 0) {
			unmap_mapping_range(inode->i_mapping, 0, 0, 0);
			ret = filemap_fdatawait(inode->i_mapping);
			if (ret < 0) {
				clear_bit(NETFS_ICTX_ODIRECT, &ictx->flags);
				return ret;
			}
		}
	}
	return 0;
}

/**
 * netfs_start_io_direct - declare the file is being used for direct i/o
 * @inode: file inode
 *
 * Declare that a direct I/O operation is about to start, and ensure
 * that we block all buffered I/O.
 * On exit, the function ensures that the NETFS_ICTX_ODIRECT flag is set,
 * and holds a shared lock on inode->i_rwsem to ensure that the flag
 * cannot be changed.
 * In practice, this means that direct I/O operations are allowed to
 * execute in parallel, thanks to the shared lock, whereas buffered I/O
 * operations need to wait to grab an exclusive lock in order to clear
 * NETFS_ICTX_ODIRECT.
 * Note that buffered writes and truncates both take a write lock on
 * inode->i_rwsem, meaning that those are serialised w.r.t. O_DIRECT.
 */
int netfs_start_io_direct(struct inode *inode)
	__acquires(inode->i_rwsem)
{
	struct netfs_inode *ictx = netfs_inode(inode);
	int ret;

	/* Be an optimist! */
	if (down_read_interruptible(&inode->i_rwsem) < 0)
		return -ERESTARTSYS;
	if (test_bit(NETFS_ICTX_ODIRECT, &ictx->flags) != 0)
		return 0;
	up_read(&inode->i_rwsem);

	/* Slow path.... */
	if (down_write_killable(&inode->i_rwsem) < 0)
		return -ERESTARTSYS;
	ret = netfs_block_buffered(inode);
	if (ret < 0) {
		up_write(&inode->i_rwsem);
		return ret;
	}
	downgrade_write(&inode->i_rwsem);
	return 0;
}
EXPORT_SYMBOL(netfs_start_io_direct);

/**
 * netfs_end_io_direct - declare that the direct i/o operation is done
 * @inode: file inode
 *
 * Declare that a direct I/O operation is done, and release the shared
 * lock on inode->i_rwsem.
 */
void netfs_end_io_direct(struct inode *inode)
	__releases(inode->i_rwsem)
{
	up_read(&inode->i_rwsem);
}
EXPORT_SYMBOL(netfs_end_io_direct);
