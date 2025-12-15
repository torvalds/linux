/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_FS_SUPER_H
#define _LINUX_FS_SUPER_H

#include <linux/fs/super_types.h>
#include <linux/unicode.h>

/*
 * These are internal functions, please use sb_start_{write,pagefault,intwrite}
 * instead.
 */
static inline void __sb_end_write(struct super_block *sb, int level)
{
	percpu_up_read(sb->s_writers.rw_sem + level - 1);
}

static inline void __sb_start_write(struct super_block *sb, int level)
{
	percpu_down_read_freezable(sb->s_writers.rw_sem + level - 1, true);
}

static inline bool __sb_start_write_trylock(struct super_block *sb, int level)
{
	return percpu_down_read_trylock(sb->s_writers.rw_sem + level - 1);
}

#define __sb_writers_acquired(sb, lev) \
	percpu_rwsem_acquire(&(sb)->s_writers.rw_sem[(lev) - 1], 1, _THIS_IP_)
#define __sb_writers_release(sb, lev) \
	percpu_rwsem_release(&(sb)->s_writers.rw_sem[(lev) - 1], _THIS_IP_)

/**
 * __sb_write_started - check if sb freeze level is held
 * @sb: the super we write to
 * @level: the freeze level
 *
 * * > 0 - sb freeze level is held
 * *   0 - sb freeze level is not held
 * * < 0 - !CONFIG_LOCKDEP/LOCK_STATE_UNKNOWN
 */
static inline int __sb_write_started(const struct super_block *sb, int level)
{
	return lockdep_is_held_type(sb->s_writers.rw_sem + level - 1, 1);
}

/**
 * sb_write_started - check if SB_FREEZE_WRITE is held
 * @sb: the super we write to
 *
 * May be false positive with !CONFIG_LOCKDEP/LOCK_STATE_UNKNOWN.
 */
static inline bool sb_write_started(const struct super_block *sb)
{
	return __sb_write_started(sb, SB_FREEZE_WRITE);
}

/**
 * sb_write_not_started - check if SB_FREEZE_WRITE is not held
 * @sb: the super we write to
 *
 * May be false positive with !CONFIG_LOCKDEP/LOCK_STATE_UNKNOWN.
 */
static inline bool sb_write_not_started(const struct super_block *sb)
{
	return __sb_write_started(sb, SB_FREEZE_WRITE) <= 0;
}

/**
 * sb_end_write - drop write access to a superblock
 * @sb: the super we wrote to
 *
 * Decrement number of writers to the filesystem. Wake up possible waiters
 * wanting to freeze the filesystem.
 */
static inline void sb_end_write(struct super_block *sb)
{
	__sb_end_write(sb, SB_FREEZE_WRITE);
}

/**
 * sb_end_pagefault - drop write access to a superblock from a page fault
 * @sb: the super we wrote to
 *
 * Decrement number of processes handling write page fault to the filesystem.
 * Wake up possible waiters wanting to freeze the filesystem.
 */
static inline void sb_end_pagefault(struct super_block *sb)
{
	__sb_end_write(sb, SB_FREEZE_PAGEFAULT);
}

/**
 * sb_end_intwrite - drop write access to a superblock for internal fs purposes
 * @sb: the super we wrote to
 *
 * Decrement fs-internal number of writers to the filesystem.  Wake up possible
 * waiters wanting to freeze the filesystem.
 */
static inline void sb_end_intwrite(struct super_block *sb)
{
	__sb_end_write(sb, SB_FREEZE_FS);
}

/**
 * sb_start_write - get write access to a superblock
 * @sb: the super we write to
 *
 * When a process wants to write data or metadata to a file system (i.e. dirty
 * a page or an inode), it should embed the operation in a sb_start_write() -
 * sb_end_write() pair to get exclusion against file system freezing. This
 * function increments number of writers preventing freezing. If the file
 * system is already frozen, the function waits until the file system is
 * thawed.
 *
 * Since freeze protection behaves as a lock, users have to preserve
 * ordering of freeze protection and other filesystem locks. Generally,
 * freeze protection should be the outermost lock. In particular, we have:
 *
 * sb_start_write
 *   -> i_rwsem			(write path, truncate, directory ops, ...)
 *   -> s_umount		(freeze_super, thaw_super)
 */
static inline void sb_start_write(struct super_block *sb)
{
	__sb_start_write(sb, SB_FREEZE_WRITE);
}

DEFINE_GUARD(super_write,
	     struct super_block *,
	     sb_start_write(_T),
	     sb_end_write(_T))

static inline bool sb_start_write_trylock(struct super_block *sb)
{
	return __sb_start_write_trylock(sb, SB_FREEZE_WRITE);
}

/**
 * sb_start_pagefault - get write access to a superblock from a page fault
 * @sb: the super we write to
 *
 * When a process starts handling write page fault, it should embed the
 * operation into sb_start_pagefault() - sb_end_pagefault() pair to get
 * exclusion against file system freezing. This is needed since the page fault
 * is going to dirty a page. This function increments number of running page
 * faults preventing freezing. If the file system is already frozen, the
 * function waits until the file system is thawed.
 *
 * Since page fault freeze protection behaves as a lock, users have to preserve
 * ordering of freeze protection and other filesystem locks. It is advised to
 * put sb_start_pagefault() close to mmap_lock in lock ordering. Page fault
 * handling code implies lock dependency:
 *
 * mmap_lock
 *   -> sb_start_pagefault
 */
static inline void sb_start_pagefault(struct super_block *sb)
{
	__sb_start_write(sb, SB_FREEZE_PAGEFAULT);
}

/**
 * sb_start_intwrite - get write access to a superblock for internal fs purposes
 * @sb: the super we write to
 *
 * This is the third level of protection against filesystem freezing. It is
 * free for use by a filesystem. The only requirement is that it must rank
 * below sb_start_pagefault.
 *
 * For example filesystem can call sb_start_intwrite() when starting a
 * transaction which somewhat eases handling of freezing for internal sources
 * of filesystem changes (internal fs threads, discarding preallocation on file
 * close, etc.).
 */
static inline void sb_start_intwrite(struct super_block *sb)
{
	__sb_start_write(sb, SB_FREEZE_FS);
}

static inline bool sb_start_intwrite_trylock(struct super_block *sb)
{
	return __sb_start_write_trylock(sb, SB_FREEZE_FS);
}

static inline bool sb_rdonly(const struct super_block *sb)
{
	return sb->s_flags & SB_RDONLY;
}

static inline bool sb_is_blkdev_sb(struct super_block *sb)
{
	return IS_ENABLED(CONFIG_BLOCK) && sb == blockdev_superblock;
}

#if IS_ENABLED(CONFIG_UNICODE)
static inline struct unicode_map *sb_encoding(const struct super_block *sb)
{
	return sb->s_encoding;
}

/* Compare if two super blocks have the same encoding and flags */
static inline bool sb_same_encoding(const struct super_block *sb1,
				    const struct super_block *sb2)
{
	if (sb1->s_encoding == sb2->s_encoding)
		return true;

	return (sb1->s_encoding && sb2->s_encoding &&
		(sb1->s_encoding->version == sb2->s_encoding->version) &&
		(sb1->s_encoding_flags == sb2->s_encoding_flags));
}
#else
static inline struct unicode_map *sb_encoding(const struct super_block *sb)
{
	return NULL;
}

static inline bool sb_same_encoding(const struct super_block *sb1,
				    const struct super_block *sb2)
{
	return true;
}
#endif

static inline bool sb_has_encoding(const struct super_block *sb)
{
	return !!sb_encoding(sb);
}

int sb_set_blocksize(struct super_block *sb, int size);
int __must_check sb_min_blocksize(struct super_block *sb, int size);

int freeze_super(struct super_block *super, enum freeze_holder who,
		 const void *freeze_owner);
int thaw_super(struct super_block *super, enum freeze_holder who,
	       const void *freeze_owner);

#endif /* _LINUX_FS_SUPER_H */
