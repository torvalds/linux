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

/* Call with exclusively locked iyesde->i_rwsem */
static void nfs_block_o_direct(struct nfs_iyesde *nfsi, struct iyesde *iyesde)
{
	if (test_bit(NFS_INO_ODIRECT, &nfsi->flags)) {
		clear_bit(NFS_INO_ODIRECT, &nfsi->flags);
		iyesde_dio_wait(iyesde);
	}
}

/**
 * nfs_start_io_read - declare the file is being used for buffered reads
 * @iyesde: file iyesde
 *
 * Declare that a buffered read operation is about to start, and ensure
 * that we block all direct I/O.
 * On exit, the function ensures that the NFS_INO_ODIRECT flag is unset,
 * and holds a shared lock on iyesde->i_rwsem to ensure that the flag
 * canyest be changed.
 * In practice, this means that buffered read operations are allowed to
 * execute in parallel, thanks to the shared lock, whereas direct I/O
 * operations need to wait to grab an exclusive lock in order to set
 * NFS_INO_ODIRECT.
 * Note that buffered writes and truncates both take a write lock on
 * iyesde->i_rwsem, meaning that those are serialised w.r.t. the reads.
 */
void
nfs_start_io_read(struct iyesde *iyesde)
{
	struct nfs_iyesde *nfsi = NFS_I(iyesde);
	/* Be an optimist! */
	down_read(&iyesde->i_rwsem);
	if (test_bit(NFS_INO_ODIRECT, &nfsi->flags) == 0)
		return;
	up_read(&iyesde->i_rwsem);
	/* Slow path.... */
	down_write(&iyesde->i_rwsem);
	nfs_block_o_direct(nfsi, iyesde);
	downgrade_write(&iyesde->i_rwsem);
}

/**
 * nfs_end_io_read - declare that the buffered read operation is done
 * @iyesde: file iyesde
 *
 * Declare that a buffered read operation is done, and release the shared
 * lock on iyesde->i_rwsem.
 */
void
nfs_end_io_read(struct iyesde *iyesde)
{
	up_read(&iyesde->i_rwsem);
}

/**
 * nfs_start_io_write - declare the file is being used for buffered writes
 * @iyesde: file iyesde
 *
 * Declare that a buffered read operation is about to start, and ensure
 * that we block all direct I/O.
 */
void
nfs_start_io_write(struct iyesde *iyesde)
{
	down_write(&iyesde->i_rwsem);
	nfs_block_o_direct(NFS_I(iyesde), iyesde);
}

/**
 * nfs_end_io_write - declare that the buffered write operation is done
 * @iyesde: file iyesde
 *
 * Declare that a buffered write operation is done, and release the
 * lock on iyesde->i_rwsem.
 */
void
nfs_end_io_write(struct iyesde *iyesde)
{
	up_write(&iyesde->i_rwsem);
}

/* Call with exclusively locked iyesde->i_rwsem */
static void nfs_block_buffered(struct nfs_iyesde *nfsi, struct iyesde *iyesde)
{
	if (!test_bit(NFS_INO_ODIRECT, &nfsi->flags)) {
		set_bit(NFS_INO_ODIRECT, &nfsi->flags);
		nfs_sync_mapping(iyesde->i_mapping);
	}
}

/**
 * nfs_end_io_direct - declare the file is being used for direct i/o
 * @iyesde: file iyesde
 *
 * Declare that a direct I/O operation is about to start, and ensure
 * that we block all buffered I/O.
 * On exit, the function ensures that the NFS_INO_ODIRECT flag is set,
 * and holds a shared lock on iyesde->i_rwsem to ensure that the flag
 * canyest be changed.
 * In practice, this means that direct I/O operations are allowed to
 * execute in parallel, thanks to the shared lock, whereas buffered I/O
 * operations need to wait to grab an exclusive lock in order to clear
 * NFS_INO_ODIRECT.
 * Note that buffered writes and truncates both take a write lock on
 * iyesde->i_rwsem, meaning that those are serialised w.r.t. O_DIRECT.
 */
void
nfs_start_io_direct(struct iyesde *iyesde)
{
	struct nfs_iyesde *nfsi = NFS_I(iyesde);
	/* Be an optimist! */
	down_read(&iyesde->i_rwsem);
	if (test_bit(NFS_INO_ODIRECT, &nfsi->flags) != 0)
		return;
	up_read(&iyesde->i_rwsem);
	/* Slow path.... */
	down_write(&iyesde->i_rwsem);
	nfs_block_buffered(nfsi, iyesde);
	downgrade_write(&iyesde->i_rwsem);
}

/**
 * nfs_end_io_direct - declare that the direct i/o operation is done
 * @iyesde: file iyesde
 *
 * Declare that a direct I/O operation is done, and release the shared
 * lock on iyesde->i_rwsem.
 */
void
nfs_end_io_direct(struct iyesde *iyesde)
{
	up_read(&iyesde->i_rwsem);
}
