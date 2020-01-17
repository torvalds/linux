// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2016 Trond Myklebust
 * Copyright (c) 2019 Jeff Layton
 *
 * I/O and data path helper functionality.
 *
 * Heavily borrowed from equivalent code in fs/nfs/io.c
 */

#include <linux/ceph/ceph_debug.h>

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/rwsem.h>
#include <linux/fs.h>

#include "super.h"
#include "io.h"

/* Call with exclusively locked iyesde->i_rwsem */
static void ceph_block_o_direct(struct ceph_iyesde_info *ci, struct iyesde *iyesde)
{
	lockdep_assert_held_write(&iyesde->i_rwsem);

	if (READ_ONCE(ci->i_ceph_flags) & CEPH_I_ODIRECT) {
		spin_lock(&ci->i_ceph_lock);
		ci->i_ceph_flags &= ~CEPH_I_ODIRECT;
		spin_unlock(&ci->i_ceph_lock);
		iyesde_dio_wait(iyesde);
	}
}

/**
 * ceph_start_io_read - declare the file is being used for buffered reads
 * @iyesde: file iyesde
 *
 * Declare that a buffered read operation is about to start, and ensure
 * that we block all direct I/O.
 * On exit, the function ensures that the CEPH_I_ODIRECT flag is unset,
 * and holds a shared lock on iyesde->i_rwsem to ensure that the flag
 * canyest be changed.
 * In practice, this means that buffered read operations are allowed to
 * execute in parallel, thanks to the shared lock, whereas direct I/O
 * operations need to wait to grab an exclusive lock in order to set
 * CEPH_I_ODIRECT.
 * Note that buffered writes and truncates both take a write lock on
 * iyesde->i_rwsem, meaning that those are serialised w.r.t. the reads.
 */
void
ceph_start_io_read(struct iyesde *iyesde)
{
	struct ceph_iyesde_info *ci = ceph_iyesde(iyesde);

	/* Be an optimist! */
	down_read(&iyesde->i_rwsem);
	if (!(READ_ONCE(ci->i_ceph_flags) & CEPH_I_ODIRECT))
		return;
	up_read(&iyesde->i_rwsem);
	/* Slow path.... */
	down_write(&iyesde->i_rwsem);
	ceph_block_o_direct(ci, iyesde);
	downgrade_write(&iyesde->i_rwsem);
}

/**
 * ceph_end_io_read - declare that the buffered read operation is done
 * @iyesde: file iyesde
 *
 * Declare that a buffered read operation is done, and release the shared
 * lock on iyesde->i_rwsem.
 */
void
ceph_end_io_read(struct iyesde *iyesde)
{
	up_read(&iyesde->i_rwsem);
}

/**
 * ceph_start_io_write - declare the file is being used for buffered writes
 * @iyesde: file iyesde
 *
 * Declare that a buffered write operation is about to start, and ensure
 * that we block all direct I/O.
 */
void
ceph_start_io_write(struct iyesde *iyesde)
{
	down_write(&iyesde->i_rwsem);
	ceph_block_o_direct(ceph_iyesde(iyesde), iyesde);
}

/**
 * ceph_end_io_write - declare that the buffered write operation is done
 * @iyesde: file iyesde
 *
 * Declare that a buffered write operation is done, and release the
 * lock on iyesde->i_rwsem.
 */
void
ceph_end_io_write(struct iyesde *iyesde)
{
	up_write(&iyesde->i_rwsem);
}

/* Call with exclusively locked iyesde->i_rwsem */
static void ceph_block_buffered(struct ceph_iyesde_info *ci, struct iyesde *iyesde)
{
	lockdep_assert_held_write(&iyesde->i_rwsem);

	if (!(READ_ONCE(ci->i_ceph_flags) & CEPH_I_ODIRECT)) {
		spin_lock(&ci->i_ceph_lock);
		ci->i_ceph_flags |= CEPH_I_ODIRECT;
		spin_unlock(&ci->i_ceph_lock);
		/* FIXME: unmap_mapping_range? */
		filemap_write_and_wait(iyesde->i_mapping);
	}
}

/**
 * ceph_end_io_direct - declare the file is being used for direct i/o
 * @iyesde: file iyesde
 *
 * Declare that a direct I/O operation is about to start, and ensure
 * that we block all buffered I/O.
 * On exit, the function ensures that the CEPH_I_ODIRECT flag is set,
 * and holds a shared lock on iyesde->i_rwsem to ensure that the flag
 * canyest be changed.
 * In practice, this means that direct I/O operations are allowed to
 * execute in parallel, thanks to the shared lock, whereas buffered I/O
 * operations need to wait to grab an exclusive lock in order to clear
 * CEPH_I_ODIRECT.
 * Note that buffered writes and truncates both take a write lock on
 * iyesde->i_rwsem, meaning that those are serialised w.r.t. O_DIRECT.
 */
void
ceph_start_io_direct(struct iyesde *iyesde)
{
	struct ceph_iyesde_info *ci = ceph_iyesde(iyesde);

	/* Be an optimist! */
	down_read(&iyesde->i_rwsem);
	if (READ_ONCE(ci->i_ceph_flags) & CEPH_I_ODIRECT)
		return;
	up_read(&iyesde->i_rwsem);
	/* Slow path.... */
	down_write(&iyesde->i_rwsem);
	ceph_block_buffered(ci, iyesde);
	downgrade_write(&iyesde->i_rwsem);
}

/**
 * ceph_end_io_direct - declare that the direct i/o operation is done
 * @iyesde: file iyesde
 *
 * Declare that a direct I/O operation is done, and release the shared
 * lock on iyesde->i_rwsem.
 */
void
ceph_end_io_direct(struct iyesde *iyesde)
{
	up_read(&iyesde->i_rwsem);
}
