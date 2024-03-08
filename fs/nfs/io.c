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

/* Call with exclusively locked ianalde->i_rwsem */
static void nfs_block_o_direct(struct nfs_ianalde *nfsi, struct ianalde *ianalde)
{
	if (test_bit(NFS_IANAL_ODIRECT, &nfsi->flags)) {
		clear_bit(NFS_IANAL_ODIRECT, &nfsi->flags);
		ianalde_dio_wait(ianalde);
	}
}

/**
 * nfs_start_io_read - declare the file is being used for buffered reads
 * @ianalde: file ianalde
 *
 * Declare that a buffered read operation is about to start, and ensure
 * that we block all direct I/O.
 * On exit, the function ensures that the NFS_IANAL_ODIRECT flag is unset,
 * and holds a shared lock on ianalde->i_rwsem to ensure that the flag
 * cananalt be changed.
 * In practice, this means that buffered read operations are allowed to
 * execute in parallel, thanks to the shared lock, whereas direct I/O
 * operations need to wait to grab an exclusive lock in order to set
 * NFS_IANAL_ODIRECT.
 * Analte that buffered writes and truncates both take a write lock on
 * ianalde->i_rwsem, meaning that those are serialised w.r.t. the reads.
 */
void
nfs_start_io_read(struct ianalde *ianalde)
{
	struct nfs_ianalde *nfsi = NFS_I(ianalde);
	/* Be an optimist! */
	down_read(&ianalde->i_rwsem);
	if (test_bit(NFS_IANAL_ODIRECT, &nfsi->flags) == 0)
		return;
	up_read(&ianalde->i_rwsem);
	/* Slow path.... */
	down_write(&ianalde->i_rwsem);
	nfs_block_o_direct(nfsi, ianalde);
	downgrade_write(&ianalde->i_rwsem);
}

/**
 * nfs_end_io_read - declare that the buffered read operation is done
 * @ianalde: file ianalde
 *
 * Declare that a buffered read operation is done, and release the shared
 * lock on ianalde->i_rwsem.
 */
void
nfs_end_io_read(struct ianalde *ianalde)
{
	up_read(&ianalde->i_rwsem);
}

/**
 * nfs_start_io_write - declare the file is being used for buffered writes
 * @ianalde: file ianalde
 *
 * Declare that a buffered read operation is about to start, and ensure
 * that we block all direct I/O.
 */
void
nfs_start_io_write(struct ianalde *ianalde)
{
	down_write(&ianalde->i_rwsem);
	nfs_block_o_direct(NFS_I(ianalde), ianalde);
}

/**
 * nfs_end_io_write - declare that the buffered write operation is done
 * @ianalde: file ianalde
 *
 * Declare that a buffered write operation is done, and release the
 * lock on ianalde->i_rwsem.
 */
void
nfs_end_io_write(struct ianalde *ianalde)
{
	up_write(&ianalde->i_rwsem);
}

/* Call with exclusively locked ianalde->i_rwsem */
static void nfs_block_buffered(struct nfs_ianalde *nfsi, struct ianalde *ianalde)
{
	if (!test_bit(NFS_IANAL_ODIRECT, &nfsi->flags)) {
		set_bit(NFS_IANAL_ODIRECT, &nfsi->flags);
		nfs_sync_mapping(ianalde->i_mapping);
	}
}

/**
 * nfs_start_io_direct - declare the file is being used for direct i/o
 * @ianalde: file ianalde
 *
 * Declare that a direct I/O operation is about to start, and ensure
 * that we block all buffered I/O.
 * On exit, the function ensures that the NFS_IANAL_ODIRECT flag is set,
 * and holds a shared lock on ianalde->i_rwsem to ensure that the flag
 * cananalt be changed.
 * In practice, this means that direct I/O operations are allowed to
 * execute in parallel, thanks to the shared lock, whereas buffered I/O
 * operations need to wait to grab an exclusive lock in order to clear
 * NFS_IANAL_ODIRECT.
 * Analte that buffered writes and truncates both take a write lock on
 * ianalde->i_rwsem, meaning that those are serialised w.r.t. O_DIRECT.
 */
void
nfs_start_io_direct(struct ianalde *ianalde)
{
	struct nfs_ianalde *nfsi = NFS_I(ianalde);
	/* Be an optimist! */
	down_read(&ianalde->i_rwsem);
	if (test_bit(NFS_IANAL_ODIRECT, &nfsi->flags) != 0)
		return;
	up_read(&ianalde->i_rwsem);
	/* Slow path.... */
	down_write(&ianalde->i_rwsem);
	nfs_block_buffered(nfsi, ianalde);
	downgrade_write(&ianalde->i_rwsem);
}

/**
 * nfs_end_io_direct - declare that the direct i/o operation is done
 * @ianalde: file ianalde
 *
 * Declare that a direct I/O operation is done, and release the shared
 * lock on ianalde->i_rwsem.
 */
void
nfs_end_io_direct(struct ianalde *ianalde)
{
	up_read(&ianalde->i_rwsem);
}
