// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2016 Trond Myklebust
 *
 * I/O and data path helper functionality.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/rwsem.h>
#include <linux/fs.h>
#include <linux/nfs_fs.h>

#include "internal.h"

/* Call with exclusively locked inode->i_rwsem */
static void nfs_block_o_direct(struct nfs_inode *nfsi, struct inode *inode)
{
	if (test_bit(NFS_INO_ODIRECT, &nfsi->flags)) {
		clear_bit(NFS_INO_ODIRECT, &nfsi->flags);
		inode_dio_wait(inode);
	}
}

/**
 * nfs_start_io_read - declare the file is being used for buffered reads
 * @inode: file inode
 *
 * Declare that a buffered read operation is about to start, and ensure
 * that we block all direct I/O.
 * On exit, the function ensures that the NFS_INO_ODIRECT flag is unset,
 * and holds a shared lock on inode->i_rwsem to ensure that the flag
 * cannot be changed.
 * In practice, this means that buffered read operations are allowed to
 * execute in parallel, thanks to the shared lock, whereas direct I/O
 * operations need to wait to grab an exclusive lock in order to set
 * NFS_INO_ODIRECT.
 * Note that buffered writes and truncates both take a write lock on
 * inode->i_rwsem, meaning that those are serialised w.r.t. the reads.
 */
int
nfs_start_io_read(struct inode *inode)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	int err;

	/* Be an optimist! */
	err = down_read_killable(&inode->i_rwsem);
	if (err)
		return err;
	if (test_bit(NFS_INO_ODIRECT, &nfsi->flags) == 0)
		return 0;
	up_read(&inode->i_rwsem);

	/* Slow path.... */
	err = down_write_killable(&inode->i_rwsem);
	if (err)
		return err;
	nfs_block_o_direct(nfsi, inode);
	downgrade_write(&inode->i_rwsem);

	return 0;
}

/**
 * nfs_end_io_read - declare that the buffered read operation is done
 * @inode: file inode
 *
 * Declare that a buffered read operation is done, and release the shared
 * lock on inode->i_rwsem.
 */
void
nfs_end_io_read(struct inode *inode)
{
	up_read(&inode->i_rwsem);
}

/**
 * nfs_start_io_write - declare the file is being used for buffered writes
 * @inode: file inode
 *
 * Declare that a buffered read operation is about to start, and ensure
 * that we block all direct I/O.
 */
int
nfs_start_io_write(struct inode *inode)
{
	int err;

	err = down_write_killable(&inode->i_rwsem);
	if (!err)
		nfs_block_o_direct(NFS_I(inode), inode);
	return err;
}

/**
 * nfs_end_io_write - declare that the buffered write operation is done
 * @inode: file inode
 *
 * Declare that a buffered write operation is done, and release the
 * lock on inode->i_rwsem.
 */
void
nfs_end_io_write(struct inode *inode)
{
	up_write(&inode->i_rwsem);
}

/* Call with exclusively locked inode->i_rwsem */
static void nfs_block_buffered(struct nfs_inode *nfsi, struct inode *inode)
{
	if (!test_bit(NFS_INO_ODIRECT, &nfsi->flags)) {
		set_bit(NFS_INO_ODIRECT, &nfsi->flags);
		nfs_sync_mapping(inode->i_mapping);
	}
}

/**
 * nfs_start_io_direct - declare the file is being used for direct i/o
 * @inode: file inode
 *
 * Declare that a direct I/O operation is about to start, and ensure
 * that we block all buffered I/O.
 * On exit, the function ensures that the NFS_INO_ODIRECT flag is set,
 * and holds a shared lock on inode->i_rwsem to ensure that the flag
 * cannot be changed.
 * In practice, this means that direct I/O operations are allowed to
 * execute in parallel, thanks to the shared lock, whereas buffered I/O
 * operations need to wait to grab an exclusive lock in order to clear
 * NFS_INO_ODIRECT.
 * Note that buffered writes and truncates both take a write lock on
 * inode->i_rwsem, meaning that those are serialised w.r.t. O_DIRECT.
 */
int
nfs_start_io_direct(struct inode *inode)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	int err;

	/* Be an optimist! */
	err = down_read_killable(&inode->i_rwsem);
	if (err)
		return err;
	if (test_bit(NFS_INO_ODIRECT, &nfsi->flags) != 0)
		return 0;
	up_read(&inode->i_rwsem);

	/* Slow path.... */
	err = down_write_killable(&inode->i_rwsem);
	if (err)
		return err;
	nfs_block_buffered(nfsi, inode);
	downgrade_write(&inode->i_rwsem);

	return 0;
}

/**
 * nfs_end_io_direct - declare that the direct i/o operation is done
 * @inode: file inode
 *
 * Declare that a direct I/O operation is done, and release the shared
 * lock on inode->i_rwsem.
 */
void
nfs_end_io_direct(struct inode *inode)
{
	up_read(&inode->i_rwsem);
}
