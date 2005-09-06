/*
 * linux/fs/revoke.c
 * 
 * Written by Stephen C. Tweedie <sct@redhat.com>, 2000
 *
 * Copyright 2000 Red Hat corp --- All Rights Reserved
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 *
 * Journal revoke routines for the generic filesystem journaling code;
 * part of the ext2fs journaling system.
 *
 * Revoke is the mechanism used to prevent old log records for deleted
 * metadata from being replayed on top of newer data using the same
 * blocks.  The revoke mechanism is used in two separate places:
 * 
 * + Commit: during commit we write the entire list of the current
 *   transaction's revoked blocks to the journal
 * 
 * + Recovery: during recovery we record the transaction ID of all
 *   revoked blocks.  If there are multiple revoke records in the log
 *   for a single block, only the last one counts, and if there is a log
 *   entry for a block beyond the last revoke, then that log entry still
 *   gets replayed.
 *
 * We can get interactions between revokes and new log data within a
 * single transaction:
 *
 * Block is revoked and then journaled:
 *   The desired end result is the journaling of the new block, so we 
 *   cancel the revoke before the transaction commits.
 *
 * Block is journaled and then revoked:
 *   The revoke must take precedence over the write of the block, so we
 *   need either to cancel the journal entry or to write the revoke
 *   later in the log than the log block.  In this case, we choose the
 *   latter: journaling a block cancels any revoke record for that block
 *   in the current transaction, so any revoke for that block in the
 *   transaction must have happened after the block was journaled and so
 *   the revoke must take precedence.
 *
 * Block is revoked and then written as data: 
 *   The data write is allowed to succeed, but the revoke is _not_
 *   cancelled.  We still need to prevent old log records from
 *   overwriting the new data.  We don't even need to clear the revoke
 *   bit here.
 *
 * Revoke information on buffers is a tri-state value:
 *
 * RevokeValid clear:	no cached revoke status, need to look it up
 * RevokeValid set, Revoked clear:
 *			buffer has not been revoked, and cancel_revoke
 *			need do nothing.
 * RevokeValid set, Revoked set:
 *			buffer has been revoked.  
 */

#ifndef __KERNEL__
#include "jfs_user.h"
#else
#include <linux/time.h>
#include <linux/fs.h>
#include <linux/jbd.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/smp_lock.h>
#include <linux/init.h>
#endif

static kmem_cache_t *revoke_record_cache;
static kmem_cache_t *revoke_table_cache;

/* Each revoke record represents one single revoked block.  During
   journal replay, this involves recording the transaction ID of the
   last transaction to revoke this block. */

struct jbd_revoke_record_s 
{
	struct list_head  hash;
	tid_t		  sequence;	/* Used for recovery only */
	unsigned long	  blocknr;
};


/* The revoke table is just a simple hash table of revoke records. */
struct jbd_revoke_table_s
{
	/* It is conceivable that we might want a larger hash table
	 * for recovery.  Must be a power of two. */
	int		  hash_size; 
	int		  hash_shift; 
	struct list_head *hash_table;
};


#ifdef __KERNEL__
static void write_one_revoke_record(journal_t *, transaction_t *,
				    struct journal_head **, int *,
				    struct jbd_revoke_record_s *);
static void flush_descriptor(journal_t *, struct journal_head *, int);
#endif

/* Utility functions to maintain the revoke table */

/* Borrowed from buffer.c: this is a tried and tested block hash function */
static inline int hash(journal_t *journal, unsigned long block)
{
	struct jbd_revoke_table_s *table = journal->j_revoke;
	int hash_shift = table->hash_shift;

	return ((block << (hash_shift - 6)) ^
		(block >> 13) ^
		(block << (hash_shift - 12))) & (table->hash_size - 1);
}

static int insert_revoke_hash(journal_t *journal, unsigned long blocknr,
			      tid_t seq)
{
	struct list_head *hash_list;
	struct jbd_revoke_record_s *record;

repeat:
	record = kmem_cache_alloc(revoke_record_cache, GFP_NOFS);
	if (!record)
		goto oom;

	record->sequence = seq;
	record->blocknr = blocknr;
	hash_list = &journal->j_revoke->hash_table[hash(journal, blocknr)];
	spin_lock(&journal->j_revoke_lock);
	list_add(&record->hash, hash_list);
	spin_unlock(&journal->j_revoke_lock);
	return 0;

oom:
	if (!journal_oom_retry)
		return -ENOMEM;
	jbd_debug(1, "ENOMEM in %s, retrying\n", __FUNCTION__);
	yield();
	goto repeat;
}

/* Find a revoke record in the journal's hash table. */

static struct jbd_revoke_record_s *find_revoke_record(journal_t *journal,
						      unsigned long blocknr)
{
	struct list_head *hash_list;
	struct jbd_revoke_record_s *record;

	hash_list = &journal->j_revoke->hash_table[hash(journal, blocknr)];

	spin_lock(&journal->j_revoke_lock);
	record = (struct jbd_revoke_record_s *) hash_list->next;
	while (&(record->hash) != hash_list) {
		if (record->blocknr == blocknr) {
			spin_unlock(&journal->j_revoke_lock);
			return record;
		}
		record = (struct jbd_revoke_record_s *) record->hash.next;
	}
	spin_unlock(&journal->j_revoke_lock);
	return NULL;
}

int __init journal_init_revoke_caches(void)
{
	revoke_record_cache = kmem_cache_create("revoke_record",
					   sizeof(struct jbd_revoke_record_s),
					   0, SLAB_HWCACHE_ALIGN, NULL, NULL);
	if (revoke_record_cache == 0)
		return -ENOMEM;

	revoke_table_cache = kmem_cache_create("revoke_table",
					   sizeof(struct jbd_revoke_table_s),
					   0, 0, NULL, NULL);
	if (revoke_table_cache == 0) {
		kmem_cache_destroy(revoke_record_cache);
		revoke_record_cache = NULL;
		return -ENOMEM;
	}
	return 0;
}

void journal_destroy_revoke_caches(void)
{
	kmem_cache_destroy(revoke_record_cache);
	revoke_record_cache = NULL;
	kmem_cache_destroy(revoke_table_cache);
	revoke_table_cache = NULL;
}

/* Initialise the revoke table for a given journal to a given size. */

int journal_init_revoke(journal_t *journal, int hash_size)
{
	int shift, tmp;

	J_ASSERT (journal->j_revoke_table[0] == NULL);

	shift = 0;
	tmp = hash_size;
	while((tmp >>= 1UL) != 0UL)
		shift++;

	journal->j_revoke_table[0] = kmem_cache_alloc(revoke_table_cache, GFP_KERNEL);
	if (!journal->j_revoke_table[0])
		return -ENOMEM;
	journal->j_revoke = journal->j_revoke_table[0];

	/* Check that the hash_size is a power of two */
	J_ASSERT ((hash_size & (hash_size-1)) == 0);

	journal->j_revoke->hash_size = hash_size;

	journal->j_revoke->hash_shift = shift;

	journal->j_revoke->hash_table =
		kmalloc(hash_size * sizeof(struct list_head), GFP_KERNEL);
	if (!journal->j_revoke->hash_table) {
		kmem_cache_free(revoke_table_cache, journal->j_revoke_table[0]);
		journal->j_revoke = NULL;
		return -ENOMEM;
	}

	for (tmp = 0; tmp < hash_size; tmp++)
		INIT_LIST_HEAD(&journal->j_revoke->hash_table[tmp]);

	journal->j_revoke_table[1] = kmem_cache_alloc(revoke_table_cache, GFP_KERNEL);
	if (!journal->j_revoke_table[1]) {
		kfree(journal->j_revoke_table[0]->hash_table);
		kmem_cache_free(revoke_table_cache, journal->j_revoke_table[0]);
		return -ENOMEM;
	}

	journal->j_revoke = journal->j_revoke_table[1];

	/* Check that the hash_size is a power of two */
	J_ASSERT ((hash_size & (hash_size-1)) == 0);

	journal->j_revoke->hash_size = hash_size;

	journal->j_revoke->hash_shift = shift;

	journal->j_revoke->hash_table =
		kmalloc(hash_size * sizeof(struct list_head), GFP_KERNEL);
	if (!journal->j_revoke->hash_table) {
		kfree(journal->j_revoke_table[0]->hash_table);
		kmem_cache_free(revoke_table_cache, journal->j_revoke_table[0]);
		kmem_cache_free(revoke_table_cache, journal->j_revoke_table[1]);
		journal->j_revoke = NULL;
		return -ENOMEM;
	}

	for (tmp = 0; tmp < hash_size; tmp++)
		INIT_LIST_HEAD(&journal->j_revoke->hash_table[tmp]);

	spin_lock_init(&journal->j_revoke_lock);

	return 0;
}

/* Destoy a journal's revoke table.  The table must already be empty! */

void journal_destroy_revoke(journal_t *journal)
{
	struct jbd_revoke_table_s *table;
	struct list_head *hash_list;
	int i;

	table = journal->j_revoke_table[0];
	if (!table)
		return;

	for (i=0; i<table->hash_size; i++) {
		hash_list = &table->hash_table[i];
		J_ASSERT (list_empty(hash_list));
	}

	kfree(table->hash_table);
	kmem_cache_free(revoke_table_cache, table);
	journal->j_revoke = NULL;

	table = journal->j_revoke_table[1];
	if (!table)
		return;

	for (i=0; i<table->hash_size; i++) {
		hash_list = &table->hash_table[i];
		J_ASSERT (list_empty(hash_list));
	}

	kfree(table->hash_table);
	kmem_cache_free(revoke_table_cache, table);
	journal->j_revoke = NULL;
}


#ifdef __KERNEL__

/* 
 * journal_revoke: revoke a given buffer_head from the journal.  This
 * prevents the block from being replayed during recovery if we take a
 * crash after this current transaction commits.  Any subsequent
 * metadata writes of the buffer in this transaction cancel the
 * revoke.  
 *
 * Note that this call may block --- it is up to the caller to make
 * sure that there are no further calls to journal_write_metadata
 * before the revoke is complete.  In ext3, this implies calling the
 * revoke before clearing the block bitmap when we are deleting
 * metadata. 
 *
 * Revoke performs a journal_forget on any buffer_head passed in as a
 * parameter, but does _not_ forget the buffer_head if the bh was only
 * found implicitly. 
 *
 * bh_in may not be a journalled buffer - it may have come off
 * the hash tables without an attached journal_head.
 *
 * If bh_in is non-zero, journal_revoke() will decrement its b_count
 * by one.
 */

int journal_revoke(handle_t *handle, unsigned long blocknr, 
		   struct buffer_head *bh_in)
{
	struct buffer_head *bh = NULL;
	journal_t *journal;
	struct block_device *bdev;
	int err;

	might_sleep();
	if (bh_in)
		BUFFER_TRACE(bh_in, "enter");

	journal = handle->h_transaction->t_journal;
	if (!journal_set_features(journal, 0, 0, JFS_FEATURE_INCOMPAT_REVOKE)){
		J_ASSERT (!"Cannot set revoke feature!");
		return -EINVAL;
	}

	bdev = journal->j_fs_dev;
	bh = bh_in;

	if (!bh) {
		bh = __find_get_block(bdev, blocknr, journal->j_blocksize);
		if (bh)
			BUFFER_TRACE(bh, "found on hash");
	}
#ifdef JBD_EXPENSIVE_CHECKING
	else {
		struct buffer_head *bh2;

		/* If there is a different buffer_head lying around in
		 * memory anywhere... */
		bh2 = __find_get_block(bdev, blocknr, journal->j_blocksize);
		if (bh2) {
			/* ... and it has RevokeValid status... */
			if (bh2 != bh && buffer_revokevalid(bh2))
				/* ...then it better be revoked too,
				 * since it's illegal to create a revoke
				 * record against a buffer_head which is
				 * not marked revoked --- that would
				 * risk missing a subsequent revoke
				 * cancel. */
				J_ASSERT_BH(bh2, buffer_revoked(bh2));
			put_bh(bh2);
		}
	}
#endif

	/* We really ought not ever to revoke twice in a row without
           first having the revoke cancelled: it's illegal to free a
           block twice without allocating it in between! */
	if (bh) {
		if (!J_EXPECT_BH(bh, !buffer_revoked(bh),
				 "inconsistent data on disk")) {
			if (!bh_in)
				brelse(bh);
			return -EIO;
		}
		set_buffer_revoked(bh);
		set_buffer_revokevalid(bh);
		if (bh_in) {
			BUFFER_TRACE(bh_in, "call journal_forget");
			journal_forget(handle, bh_in);
		} else {
			BUFFER_TRACE(bh, "call brelse");
			__brelse(bh);
		}
	}

	jbd_debug(2, "insert revoke for block %lu, bh_in=%p\n", blocknr, bh_in);
	err = insert_revoke_hash(journal, blocknr,
				handle->h_transaction->t_tid);
	BUFFER_TRACE(bh_in, "exit");
	return err;
}

/*
 * Cancel an outstanding revoke.  For use only internally by the
 * journaling code (called from journal_get_write_access).
 *
 * We trust buffer_revoked() on the buffer if the buffer is already
 * being journaled: if there is no revoke pending on the buffer, then we
 * don't do anything here.
 *
 * This would break if it were possible for a buffer to be revoked and
 * discarded, and then reallocated within the same transaction.  In such
 * a case we would have lost the revoked bit, but when we arrived here
 * the second time we would still have a pending revoke to cancel.  So,
 * do not trust the Revoked bit on buffers unless RevokeValid is also
 * set.
 *
 * The caller must have the journal locked.
 */
int journal_cancel_revoke(handle_t *handle, struct journal_head *jh)
{
	struct jbd_revoke_record_s *record;
	journal_t *journal = handle->h_transaction->t_journal;
	int need_cancel;
	int did_revoke = 0;	/* akpm: debug */
	struct buffer_head *bh = jh2bh(jh);

	jbd_debug(4, "journal_head %p, cancelling revoke\n", jh);

	/* Is the existing Revoke bit valid?  If so, we trust it, and
	 * only perform the full cancel if the revoke bit is set.  If
	 * not, we can't trust the revoke bit, and we need to do the
	 * full search for a revoke record. */
	if (test_set_buffer_revokevalid(bh)) {
		need_cancel = test_clear_buffer_revoked(bh);
	} else {
		need_cancel = 1;
		clear_buffer_revoked(bh);
	}

	if (need_cancel) {
		record = find_revoke_record(journal, bh->b_blocknr);
		if (record) {
			jbd_debug(4, "cancelled existing revoke on "
				  "blocknr %llu\n", (unsigned long long)bh->b_blocknr);
			spin_lock(&journal->j_revoke_lock);
			list_del(&record->hash);
			spin_unlock(&journal->j_revoke_lock);
			kmem_cache_free(revoke_record_cache, record);
			did_revoke = 1;
		}
	}

#ifdef JBD_EXPENSIVE_CHECKING
	/* There better not be one left behind by now! */
	record = find_revoke_record(journal, bh->b_blocknr);
	J_ASSERT_JH(jh, record == NULL);
#endif

	/* Finally, have we just cleared revoke on an unhashed
	 * buffer_head?  If so, we'd better make sure we clear the
	 * revoked status on any hashed alias too, otherwise the revoke
	 * state machine will get very upset later on. */
	if (need_cancel) {
		struct buffer_head *bh2;
		bh2 = __find_get_block(bh->b_bdev, bh->b_blocknr, bh->b_size);
		if (bh2) {
			if (bh2 != bh)
				clear_buffer_revoked(bh2);
			__brelse(bh2);
		}
	}
	return did_revoke;
}

/* journal_switch_revoke table select j_revoke for next transaction
 * we do not want to suspend any processing until all revokes are
 * written -bzzz
 */
void journal_switch_revoke_table(journal_t *journal)
{
	int i;

	if (journal->j_revoke == journal->j_revoke_table[0])
		journal->j_revoke = journal->j_revoke_table[1];
	else
		journal->j_revoke = journal->j_revoke_table[0];

	for (i = 0; i < journal->j_revoke->hash_size; i++) 
		INIT_LIST_HEAD(&journal->j_revoke->hash_table[i]);
}

/*
 * Write revoke records to the journal for all entries in the current
 * revoke hash, deleting the entries as we go.
 *
 * Called with the journal lock held.
 */

void journal_write_revoke_records(journal_t *journal, 
				  transaction_t *transaction)
{
	struct journal_head *descriptor;
	struct jbd_revoke_record_s *record;
	struct jbd_revoke_table_s *revoke;
	struct list_head *hash_list;
	int i, offset, count;

	descriptor = NULL; 
	offset = 0;
	count = 0;

	/* select revoke table for committing transaction */
	revoke = journal->j_revoke == journal->j_revoke_table[0] ?
		journal->j_revoke_table[1] : journal->j_revoke_table[0];

	for (i = 0; i < revoke->hash_size; i++) {
		hash_list = &revoke->hash_table[i];

		while (!list_empty(hash_list)) {
			record = (struct jbd_revoke_record_s *) 
				hash_list->next;
			write_one_revoke_record(journal, transaction,
						&descriptor, &offset, 
						record);
			count++;
			list_del(&record->hash);
			kmem_cache_free(revoke_record_cache, record);
		}
	}
	if (descriptor)
		flush_descriptor(journal, descriptor, offset);
	jbd_debug(1, "Wrote %d revoke records\n", count);
}

/* 
 * Write out one revoke record.  We need to create a new descriptor
 * block if the old one is full or if we have not already created one.  
 */

static void write_one_revoke_record(journal_t *journal, 
				    transaction_t *transaction,
				    struct journal_head **descriptorp, 
				    int *offsetp,
				    struct jbd_revoke_record_s *record)
{
	struct journal_head *descriptor;
	int offset;
	journal_header_t *header;

	/* If we are already aborting, this all becomes a noop.  We
           still need to go round the loop in
           journal_write_revoke_records in order to free all of the
           revoke records: only the IO to the journal is omitted. */
	if (is_journal_aborted(journal))
		return;

	descriptor = *descriptorp;
	offset = *offsetp;

	/* Make sure we have a descriptor with space left for the record */
	if (descriptor) {
		if (offset == journal->j_blocksize) {
			flush_descriptor(journal, descriptor, offset);
			descriptor = NULL;
		}
	}

	if (!descriptor) {
		descriptor = journal_get_descriptor_buffer(journal);
		if (!descriptor)
			return;
		header = (journal_header_t *) &jh2bh(descriptor)->b_data[0];
		header->h_magic     = cpu_to_be32(JFS_MAGIC_NUMBER);
		header->h_blocktype = cpu_to_be32(JFS_REVOKE_BLOCK);
		header->h_sequence  = cpu_to_be32(transaction->t_tid);

		/* Record it so that we can wait for IO completion later */
		JBUFFER_TRACE(descriptor, "file as BJ_LogCtl");
		journal_file_buffer(descriptor, transaction, BJ_LogCtl);

		offset = sizeof(journal_revoke_header_t);
		*descriptorp = descriptor;
	}

	* ((__be32 *)(&jh2bh(descriptor)->b_data[offset])) = 
		cpu_to_be32(record->blocknr);
	offset += 4;
	*offsetp = offset;
}

/* 
 * Flush a revoke descriptor out to the journal.  If we are aborting,
 * this is a noop; otherwise we are generating a buffer which needs to
 * be waited for during commit, so it has to go onto the appropriate
 * journal buffer list.
 */

static void flush_descriptor(journal_t *journal, 
			     struct journal_head *descriptor, 
			     int offset)
{
	journal_revoke_header_t *header;
	struct buffer_head *bh = jh2bh(descriptor);

	if (is_journal_aborted(journal)) {
		put_bh(bh);
		return;
	}

	header = (journal_revoke_header_t *) jh2bh(descriptor)->b_data;
	header->r_count = cpu_to_be32(offset);
	set_buffer_jwrite(bh);
	BUFFER_TRACE(bh, "write");
	set_buffer_dirty(bh);
	ll_rw_block(SWRITE, 1, &bh);
}
#endif

/* 
 * Revoke support for recovery.
 *
 * Recovery needs to be able to:
 *
 *  record all revoke records, including the tid of the latest instance
 *  of each revoke in the journal
 *
 *  check whether a given block in a given transaction should be replayed
 *  (ie. has not been revoked by a revoke record in that or a subsequent
 *  transaction)
 * 
 *  empty the revoke table after recovery.
 */

/*
 * First, setting revoke records.  We create a new revoke record for
 * every block ever revoked in the log as we scan it for recovery, and
 * we update the existing records if we find multiple revokes for a
 * single block. 
 */

int journal_set_revoke(journal_t *journal, 
		       unsigned long blocknr, 
		       tid_t sequence)
{
	struct jbd_revoke_record_s *record;

	record = find_revoke_record(journal, blocknr);
	if (record) {
		/* If we have multiple occurrences, only record the
		 * latest sequence number in the hashed record */
		if (tid_gt(sequence, record->sequence))
			record->sequence = sequence;
		return 0;
	} 
	return insert_revoke_hash(journal, blocknr, sequence);
}

/* 
 * Test revoke records.  For a given block referenced in the log, has
 * that block been revoked?  A revoke record with a given transaction
 * sequence number revokes all blocks in that transaction and earlier
 * ones, but later transactions still need replayed.
 */

int journal_test_revoke(journal_t *journal, 
			unsigned long blocknr,
			tid_t sequence)
{
	struct jbd_revoke_record_s *record;

	record = find_revoke_record(journal, blocknr);
	if (!record)
		return 0;
	if (tid_gt(sequence, record->sequence))
		return 0;
	return 1;
}

/*
 * Finally, once recovery is over, we need to clear the revoke table so
 * that it can be reused by the running filesystem.
 */

void journal_clear_revoke(journal_t *journal)
{
	int i;
	struct list_head *hash_list;
	struct jbd_revoke_record_s *record;
	struct jbd_revoke_table_s *revoke;

	revoke = journal->j_revoke;

	for (i = 0; i < revoke->hash_size; i++) {
		hash_list = &revoke->hash_table[i];
		while (!list_empty(hash_list)) {
			record = (struct jbd_revoke_record_s*) hash_list->next;
			list_del(&record->hash);
			kmem_cache_free(revoke_record_cache, record);
		}
	}
}
