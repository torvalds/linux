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

/* Call with exclusively locked inode->i_rwsem */
static void ceph_block_o_direct(struct ceph_inode_info *ci, struct inode *inode)
{
	bool is_odirect;

	lockdep_assert_held_write(&inode->i_rwsem);

	spin_lock(&ci->i_ceph_lock);
	/* ensure that bit state is consistent */
	smp_mb__before_atomic();
	is_odirect = READ_ONCE(ci->i_ceph_flags) & CEPH_I_ODIRECT;
	if (is_odirect) {
		clear_bit(CEPH_I_ODIRECT_BIT, &ci->i_ceph_flags);
		/* ensure modified bit is visible */
		smp_mb__after_atomic();
	}
	spin_unlock(&ci->i_ceph_lock);

	if (is_odirect)
		inode_dio_wait(inode);
}

/**
 * ceph_start_io_read - declare the file is being used for buffered reads
 * @inode: file inode
 *
 * Declare that a buffered read operation is about to start, and ensure
 * that we block all direct I/O.
 * On exit, the function ensures that the CEPH_I_ODIRECT flag is unset,
 * and holds a shared lock on inode->i_rwsem to ensure that the flag
 * cannot be changed.
 * In practice, this means that buffered read operations are allowed to
 * execute in parallel, thanks to the shared lock, whereas direct I/O
 * operations need to wait to grab an exclusive lock in order to set
 * CEPH_I_ODIRECT.
 * Note that buffered writes and truncates both take a write lock on
 * inode->i_rwsem, meaning that those are serialised w.r.t. the reads.
 */
int ceph_start_io_read(struct inode *inode)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	bool is_odirect;
	int err;

	/* Be an optimist! */
	err = down_read_killable(&inode->i_rwsem);
	if (err)
		return err;

	spin_lock(&ci->i_ceph_lock);
	/* ensure that bit state is consistent */
	smp_mb__before_atomic();
	is_odirect = READ_ONCE(ci->i_ceph_flags) & CEPH_I_ODIRECT;
	spin_unlock(&ci->i_ceph_lock);
	if (!is_odirect)
		return 0;
	up_read(&inode->i_rwsem);

	/* Slow path.... */
	err = down_write_killable(&inode->i_rwsem);
	if (err)
		return err;

	ceph_block_o_direct(ci, inode);
	downgrade_write(&inode->i_rwsem);

	return 0;
}

/**
 * ceph_end_io_read - declare that the buffered read operation is done
 * @inode: file inode
 *
 * Declare that a buffered read operation is done, and release the shared
 * lock on inode->i_rwsem.
 */
void
ceph_end_io_read(struct inode *inode)
{
	up_read(&inode->i_rwsem);
}

/**
 * ceph_start_io_write - declare the file is being used for buffered writes
 * @inode: file inode
 *
 * Declare that a buffered write operation is about to start, and ensure
 * that we block all direct I/O.
 */
int ceph_start_io_write(struct inode *inode)
{
	int err = down_write_killable(&inode->i_rwsem);
	if (!err)
		ceph_block_o_direct(ceph_inode(inode), inode);
	return err;
}

/**
 * ceph_end_io_write - declare that the buffered write operation is done
 * @inode: file inode
 *
 * Declare that a buffered write operation is done, and release the
 * lock on inode->i_rwsem.
 */
void
ceph_end_io_write(struct inode *inode)
{
	up_write(&inode->i_rwsem);
}

/* Call with exclusively locked inode->i_rwsem */
static void ceph_block_buffered(struct ceph_inode_info *ci, struct inode *inode)
{
	bool is_odirect;

	lockdep_assert_held_write(&inode->i_rwsem);

	spin_lock(&ci->i_ceph_lock);
	/* ensure that bit state is consistent */
	smp_mb__before_atomic();
	is_odirect = READ_ONCE(ci->i_ceph_flags) & CEPH_I_ODIRECT;
	if (!is_odirect) {
		set_bit(CEPH_I_ODIRECT_BIT, &ci->i_ceph_flags);
		/* ensure modified bit is visible */
		smp_mb__after_atomic();
	}
	spin_unlock(&ci->i_ceph_lock);

	if (!is_odirect) {
		/* FIXME: unmap_mapping_range? */
		filemap_write_and_wait(inode->i_mapping);
	}
}

/**
 * ceph_start_io_direct - declare the file is being used for direct i/o
 * @inode: file inode
 *
 * Declare that a direct I/O operation is about to start, and ensure
 * that we block all buffered I/O.
 * On exit, the function ensures that the CEPH_I_ODIRECT flag is set,
 * and holds a shared lock on inode->i_rwsem to ensure that the flag
 * cannot be changed.
 * In practice, this means that direct I/O operations are allowed to
 * execute in parallel, thanks to the shared lock, whereas buffered I/O
 * operations need to wait to grab an exclusive lock in order to clear
 * CEPH_I_ODIRECT.
 * Note that buffered writes and truncates both take a write lock on
 * inode->i_rwsem, meaning that those are serialised w.r.t. O_DIRECT.
 */
int ceph_start_io_direct(struct inode *inode)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	bool is_odirect;
	int err;

	/* Be an optimist! */
	err = down_read_killable(&inode->i_rwsem);
	if (err)
		return err;

	spin_lock(&ci->i_ceph_lock);
	/* ensure that bit state is consistent */
	smp_mb__before_atomic();
	is_odirect = READ_ONCE(ci->i_ceph_flags) & CEPH_I_ODIRECT;
	spin_unlock(&ci->i_ceph_lock);
	if (is_odirect)
		return 0;
	up_read(&inode->i_rwsem);

	/* Slow path.... */
	err = down_write_killable(&inode->i_rwsem);
	if (err)
		return err;

	ceph_block_buffered(ci, inode);
	downgrade_write(&inode->i_rwsem);

	return 0;
}

/**
 * ceph_end_io_direct - declare that the direct i/o operation is done
 * @inode: file inode
 *
 * Declare that a direct I/O operation is done, and release the shared
 * lock on inode->i_rwsem.
 */
void
ceph_end_io_direct(struct inode *inode)
{
	up_read(&inode->i_rwsem);
}
