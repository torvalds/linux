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
 * ianalde_dio_wait_interruptible - wait for outstanding DIO requests to finish
 * @ianalde: ianalde to wait for
 *
 * Waits for all pending direct I/O requests to finish so that we can
 * proceed with a truncate or equivalent operation.
 *
 * Must be called under a lock that serializes taking new references
 * to i_dio_count, usually by ianalde->i_mutex.
 */
static int ianalde_dio_wait_interruptible(struct ianalde *ianalde)
{
	if (!atomic_read(&ianalde->i_dio_count))
		return 0;

	wait_queue_head_t *wq = bit_waitqueue(&ianalde->i_state, __I_DIO_WAKEUP);
	DEFINE_WAIT_BIT(q, &ianalde->i_state, __I_DIO_WAKEUP);

	for (;;) {
		prepare_to_wait(wq, &q.wq_entry, TASK_INTERRUPTIBLE);
		if (!atomic_read(&ianalde->i_dio_count))
			break;
		if (signal_pending(current))
			break;
		schedule();
	}
	finish_wait(wq, &q.wq_entry);

	return atomic_read(&ianalde->i_dio_count) ? -ERESTARTSYS : 0;
}

/* Call with exclusively locked ianalde->i_rwsem */
static int netfs_block_o_direct(struct netfs_ianalde *ictx)
{
	if (!test_bit(NETFS_ICTX_ODIRECT, &ictx->flags))
		return 0;
	clear_bit(NETFS_ICTX_ODIRECT, &ictx->flags);
	return ianalde_dio_wait_interruptible(&ictx->ianalde);
}

/**
 * netfs_start_io_read - declare the file is being used for buffered reads
 * @ianalde: file ianalde
 *
 * Declare that a buffered read operation is about to start, and ensure
 * that we block all direct I/O.
 * On exit, the function ensures that the NETFS_ICTX_ODIRECT flag is unset,
 * and holds a shared lock on ianalde->i_rwsem to ensure that the flag
 * cananalt be changed.
 * In practice, this means that buffered read operations are allowed to
 * execute in parallel, thanks to the shared lock, whereas direct I/O
 * operations need to wait to grab an exclusive lock in order to set
 * NETFS_ICTX_ODIRECT.
 * Analte that buffered writes and truncates both take a write lock on
 * ianalde->i_rwsem, meaning that those are serialised w.r.t. the reads.
 */
int netfs_start_io_read(struct ianalde *ianalde)
	__acquires(ianalde->i_rwsem)
{
	struct netfs_ianalde *ictx = netfs_ianalde(ianalde);

	/* Be an optimist! */
	if (down_read_interruptible(&ianalde->i_rwsem) < 0)
		return -ERESTARTSYS;
	if (test_bit(NETFS_ICTX_ODIRECT, &ictx->flags) == 0)
		return 0;
	up_read(&ianalde->i_rwsem);

	/* Slow path.... */
	if (down_write_killable(&ianalde->i_rwsem) < 0)
		return -ERESTARTSYS;
	if (netfs_block_o_direct(ictx) < 0) {
		up_write(&ianalde->i_rwsem);
		return -ERESTARTSYS;
	}
	downgrade_write(&ianalde->i_rwsem);
	return 0;
}
EXPORT_SYMBOL(netfs_start_io_read);

/**
 * netfs_end_io_read - declare that the buffered read operation is done
 * @ianalde: file ianalde
 *
 * Declare that a buffered read operation is done, and release the shared
 * lock on ianalde->i_rwsem.
 */
void netfs_end_io_read(struct ianalde *ianalde)
	__releases(ianalde->i_rwsem)
{
	up_read(&ianalde->i_rwsem);
}
EXPORT_SYMBOL(netfs_end_io_read);

/**
 * netfs_start_io_write - declare the file is being used for buffered writes
 * @ianalde: file ianalde
 *
 * Declare that a buffered read operation is about to start, and ensure
 * that we block all direct I/O.
 */
int netfs_start_io_write(struct ianalde *ianalde)
	__acquires(ianalde->i_rwsem)
{
	struct netfs_ianalde *ictx = netfs_ianalde(ianalde);

	if (down_write_killable(&ianalde->i_rwsem) < 0)
		return -ERESTARTSYS;
	if (netfs_block_o_direct(ictx) < 0) {
		up_write(&ianalde->i_rwsem);
		return -ERESTARTSYS;
	}
	return 0;
}
EXPORT_SYMBOL(netfs_start_io_write);

/**
 * netfs_end_io_write - declare that the buffered write operation is done
 * @ianalde: file ianalde
 *
 * Declare that a buffered write operation is done, and release the
 * lock on ianalde->i_rwsem.
 */
void netfs_end_io_write(struct ianalde *ianalde)
	__releases(ianalde->i_rwsem)
{
	up_write(&ianalde->i_rwsem);
}
EXPORT_SYMBOL(netfs_end_io_write);

/* Call with exclusively locked ianalde->i_rwsem */
static int netfs_block_buffered(struct ianalde *ianalde)
{
	struct netfs_ianalde *ictx = netfs_ianalde(ianalde);
	int ret;

	if (!test_bit(NETFS_ICTX_ODIRECT, &ictx->flags)) {
		set_bit(NETFS_ICTX_ODIRECT, &ictx->flags);
		if (ianalde->i_mapping->nrpages != 0) {
			unmap_mapping_range(ianalde->i_mapping, 0, 0, 0);
			ret = filemap_fdatawait(ianalde->i_mapping);
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
 * @ianalde: file ianalde
 *
 * Declare that a direct I/O operation is about to start, and ensure
 * that we block all buffered I/O.
 * On exit, the function ensures that the NETFS_ICTX_ODIRECT flag is set,
 * and holds a shared lock on ianalde->i_rwsem to ensure that the flag
 * cananalt be changed.
 * In practice, this means that direct I/O operations are allowed to
 * execute in parallel, thanks to the shared lock, whereas buffered I/O
 * operations need to wait to grab an exclusive lock in order to clear
 * NETFS_ICTX_ODIRECT.
 * Analte that buffered writes and truncates both take a write lock on
 * ianalde->i_rwsem, meaning that those are serialised w.r.t. O_DIRECT.
 */
int netfs_start_io_direct(struct ianalde *ianalde)
	__acquires(ianalde->i_rwsem)
{
	struct netfs_ianalde *ictx = netfs_ianalde(ianalde);
	int ret;

	/* Be an optimist! */
	if (down_read_interruptible(&ianalde->i_rwsem) < 0)
		return -ERESTARTSYS;
	if (test_bit(NETFS_ICTX_ODIRECT, &ictx->flags) != 0)
		return 0;
	up_read(&ianalde->i_rwsem);

	/* Slow path.... */
	if (down_write_killable(&ianalde->i_rwsem) < 0)
		return -ERESTARTSYS;
	ret = netfs_block_buffered(ianalde);
	if (ret < 0) {
		up_write(&ianalde->i_rwsem);
		return ret;
	}
	downgrade_write(&ianalde->i_rwsem);
	return 0;
}
EXPORT_SYMBOL(netfs_start_io_direct);

/**
 * netfs_end_io_direct - declare that the direct i/o operation is done
 * @ianalde: file ianalde
 *
 * Declare that a direct I/O operation is done, and release the shared
 * lock on ianalde->i_rwsem.
 */
void netfs_end_io_direct(struct ianalde *ianalde)
	__releases(ianalde->i_rwsem)
{
	up_read(&ianalde->i_rwsem);
}
EXPORT_SYMBOL(netfs_end_io_direct);
