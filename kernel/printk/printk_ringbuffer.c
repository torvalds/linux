// SPDX-License-Identifier: GPL-2.0

#include <linux/kernel.h>
#include <linux/irqflags.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/bug.h>
#include "printk_ringbuffer.h"

/**
 * DOC: printk_ringbuffer overview
 *
 * Data Structure
 * --------------
 * The printk_ringbuffer is made up of 3 internal ringbuffers:
 *
 *   desc_ring
 *     A ring of descriptors and their meta data (such as sequence number,
 *     timestamp, loglevel, etc.) as well as internal state information about
 *     the record and logical positions specifying where in the other
 *     ringbuffer the text strings are located.
 *
 *   text_data_ring
 *     A ring of data blocks. A data block consists of an unsigned long
 *     integer (ID) that maps to a desc_ring index followed by the text
 *     string of the record.
 *
 * The internal state information of a descriptor is the key element to allow
 * readers and writers to locklessly synchronize access to the data.
 *
 * Implementation
 * --------------
 *
 * Descriptor Ring
 * ~~~~~~~~~~~~~~~
 * The descriptor ring is an array of descriptors. A descriptor contains
 * essential meta data to track the data of a printk record using
 * blk_lpos structs pointing to associated text data blocks (see
 * "Data Rings" below). Each descriptor is assigned an ID that maps
 * directly to index values of the descriptor array and has a state. The ID
 * and the state are bitwise combined into a single descriptor field named
 * @state_var, allowing ID and state to be synchronously and atomically
 * updated.
 *
 * Descriptors have four states:
 *
 *   reserved
 *     A writer is modifying the record.
 *
 *   committed
 *     The record and all its data are written. A writer can reopen the
 *     descriptor (transitioning it back to reserved), but in the committed
 *     state the data is consistent.
 *
 *   finalized
 *     The record and all its data are complete and available for reading. A
 *     writer cannot reopen the descriptor.
 *
 *   reusable
 *     The record exists, but its text and/or meta data may no longer be
 *     available.
 *
 * Querying the @state_var of a record requires providing the ID of the
 * descriptor to query. This can yield a possible fifth (pseudo) state:
 *
 *   miss
 *     The descriptor being queried has an unexpected ID.
 *
 * The descriptor ring has a @tail_id that contains the ID of the oldest
 * descriptor and @head_id that contains the ID of the newest descriptor.
 *
 * When a new descriptor should be created (and the ring is full), the tail
 * descriptor is invalidated by first transitioning to the reusable state and
 * then invalidating all tail data blocks up to and including the data blocks
 * associated with the tail descriptor (for the text ring). Then
 * @tail_id is advanced, followed by advancing @head_id. And finally the
 * @state_var of the new descriptor is initialized to the new ID and reserved
 * state.
 *
 * The @tail_id can only be advanced if the new @tail_id would be in the
 * committed or reusable queried state. This makes it possible that a valid
 * sequence number of the tail is always available.
 *
 * Descriptor Finalization
 * ~~~~~~~~~~~~~~~~~~~~~~~
 * When a writer calls the commit function prb_commit(), record data is
 * fully stored and is consistent within the ringbuffer. However, a writer can
 * reopen that record, claiming exclusive access (as with prb_reserve()), and
 * modify that record. When finished, the writer must again commit the record.
 *
 * In order for a record to be made available to readers (and also become
 * recyclable for writers), it must be finalized. A finalized record cannot be
 * reopened and can never become "unfinalized". Record finalization can occur
 * in three different scenarios:
 *
 *   1) A writer can simultaneously commit and finalize its record by calling
 *      prb_final_commit() instead of prb_commit().
 *
 *   2) When a new record is reserved and the previous record has been
 *      committed via prb_commit(), that previous record is automatically
 *      finalized.
 *
 *   3) When a record is committed via prb_commit() and a newer record
 *      already exists, the record being committed is automatically finalized.
 *
 * Data Ring
 * ~~~~~~~~~
 * The text data ring is a byte array composed of data blocks. Data blocks are
 * referenced by blk_lpos structs that point to the logical position of the
 * beginning of a data block and the beginning of the next adjacent data
 * block. Logical positions are mapped directly to index values of the byte
 * array ringbuffer.
 *
 * Each data block consists of an ID followed by the writer data. The ID is
 * the identifier of a descriptor that is associated with the data block. A
 * given data block is considered valid if all of the following conditions
 * are met:
 *
 *   1) The descriptor associated with the data block is in the committed
 *      or finalized queried state.
 *
 *   2) The blk_lpos struct within the descriptor associated with the data
 *      block references back to the same data block.
 *
 *   3) The data block is within the head/tail logical position range.
 *
 * If the writer data of a data block would extend beyond the end of the
 * byte array, only the ID of the data block is stored at the logical
 * position and the full data block (ID and writer data) is stored at the
 * beginning of the byte array. The referencing blk_lpos will point to the
 * ID before the wrap and the next data block will be at the logical
 * position adjacent the full data block after the wrap.
 *
 * Data rings have a @tail_lpos that points to the beginning of the oldest
 * data block and a @head_lpos that points to the logical position of the
 * next (not yet existing) data block.
 *
 * When a new data block should be created (and the ring is full), tail data
 * blocks will first be invalidated by putting their associated descriptors
 * into the reusable state and then pushing the @tail_lpos forward beyond
 * them. Then the @head_lpos is pushed forward and is associated with a new
 * descriptor. If a data block is not valid, the @tail_lpos cannot be
 * advanced beyond it.
 *
 * Info Array
 * ~~~~~~~~~~
 * The general meta data of printk records are stored in printk_info structs,
 * stored in an array with the same number of elements as the descriptor ring.
 * Each info corresponds to the descriptor of the same index in the
 * descriptor ring. Info validity is confirmed by evaluating the corresponding
 * descriptor before and after loading the info.
 *
 * Usage
 * -----
 * Here are some simple examples demonstrating writers and readers. For the
 * examples a global ringbuffer (test_rb) is available (which is not the
 * actual ringbuffer used by printk)::
 *
 *	DEFINE_PRINTKRB(test_rb, 15, 5);
 *
 * This ringbuffer allows up to 32768 records (2 ^ 15) and has a size of
 * 1 MiB (2 ^ (15 + 5)) for text data.
 *
 * Sample writer code::
 *
 *	const char *textstr = "message text";
 *	struct prb_reserved_entry e;
 *	struct printk_record r;
 *
 *	// specify how much to allocate
 *	prb_rec_init_wr(&r, strlen(textstr) + 1);
 *
 *	if (prb_reserve(&e, &test_rb, &r)) {
 *		snprintf(r.text_buf, r.text_buf_size, "%s", textstr);
 *
 *		r.info->text_len = strlen(textstr);
 *		r.info->ts_nsec = local_clock();
 *		r.info->caller_id = printk_caller_id();
 *
 *		// commit and finalize the record
 *		prb_final_commit(&e);
 *	}
 *
 * Note that additional writer functions are available to extend a record
 * after it has been committed but not yet finalized. This can be done as
 * long as no new records have been reserved and the caller is the same.
 *
 * Sample writer code (record extending)::
 *
 *		// alternate rest of previous example
 *
 *		r.info->text_len = strlen(textstr);
 *		r.info->ts_nsec = local_clock();
 *		r.info->caller_id = printk_caller_id();
 *
 *		// commit the record (but do not finalize yet)
 *		prb_commit(&e);
 *	}
 *
 *	...
 *
 *	// specify additional 5 bytes text space to extend
 *	prb_rec_init_wr(&r, 5);
 *
 *	// try to extend, but only if it does not exceed 32 bytes
 *	if (prb_reserve_in_last(&e, &test_rb, &r, printk_caller_id()), 32) {
 *		snprintf(&r.text_buf[r.info->text_len],
 *			 r.text_buf_size - r.info->text_len, "hello");
 *
 *		r.info->text_len += 5;
 *
 *		// commit and finalize the record
 *		prb_final_commit(&e);
 *	}
 *
 * Sample reader code::
 *
 *	struct printk_info info;
 *	struct printk_record r;
 *	char text_buf[32];
 *	u64 seq;
 *
 *	prb_rec_init_rd(&r, &info, &text_buf[0], sizeof(text_buf));
 *
 *	prb_for_each_record(0, &test_rb, &seq, &r) {
 *		if (info.seq != seq)
 *			pr_warn("lost %llu records\n", info.seq - seq);
 *
 *		if (info.text_len > r.text_buf_size) {
 *			pr_warn("record %llu text truncated\n", info.seq);
 *			text_buf[r.text_buf_size - 1] = 0;
 *		}
 *
 *		pr_info("%llu: %llu: %s\n", info.seq, info.ts_nsec,
 *			&text_buf[0]);
 *	}
 *
 * Note that additional less convenient reader functions are available to
 * allow complex record access.
 *
 * ABA Issues
 * ~~~~~~~~~~
 * To help avoid ABA issues, descriptors are referenced by IDs (array index
 * values combined with tagged bits counting array wraps) and data blocks are
 * referenced by logical positions (array index values combined with tagged
 * bits counting array wraps). However, on 32-bit systems the number of
 * tagged bits is relatively small such that an ABA incident is (at least
 * theoretically) possible. For example, if 4 million maximally sized (1KiB)
 * printk messages were to occur in NMI context on a 32-bit system, the
 * interrupted context would not be able to recognize that the 32-bit integer
 * completely wrapped and thus represents a different data block than the one
 * the interrupted context expects.
 *
 * To help combat this possibility, additional state checking is performed
 * (such as using cmpxchg() even though set() would suffice). These extra
 * checks are commented as such and will hopefully catch any ABA issue that
 * a 32-bit system might experience.
 *
 * Memory Barriers
 * ~~~~~~~~~~~~~~~
 * Multiple memory barriers are used. To simplify proving correctness and
 * generating litmus tests, lines of code related to memory barriers
 * (loads, stores, and the associated memory barriers) are labeled::
 *
 *	LMM(function:letter)
 *
 * Comments reference the labels using only the "function:letter" part.
 *
 * The memory barrier pairs and their ordering are:
 *
 *   desc_reserve:D / desc_reserve:B
 *     push descriptor tail (id), then push descriptor head (id)
 *
 *   desc_reserve:D / data_push_tail:B
 *     push data tail (lpos), then set new descriptor reserved (state)
 *
 *   desc_reserve:D / desc_push_tail:C
 *     push descriptor tail (id), then set new descriptor reserved (state)
 *
 *   desc_reserve:D / prb_first_seq:C
 *     push descriptor tail (id), then set new descriptor reserved (state)
 *
 *   desc_reserve:F / desc_read:D
 *     set new descriptor id and reserved (state), then allow writer changes
 *
 *   data_alloc:A (or data_realloc:A) / desc_read:D
 *     set old descriptor reusable (state), then modify new data block area
 *
 *   data_alloc:A (or data_realloc:A) / data_push_tail:B
 *     push data tail (lpos), then modify new data block area
 *
 *   _prb_commit:B / desc_read:B
 *     store writer changes, then set new descriptor committed (state)
 *
 *   desc_reopen_last:A / _prb_commit:B
 *     set descriptor reserved (state), then read descriptor data
 *
 *   _prb_commit:B / desc_reserve:D
 *     set new descriptor committed (state), then check descriptor head (id)
 *
 *   data_push_tail:D / data_push_tail:A
 *     set descriptor reusable (state), then push data tail (lpos)
 *
 *   desc_push_tail:B / desc_reserve:D
 *     set descriptor reusable (state), then push descriptor tail (id)
 */

#define DATA_SIZE(data_ring)		_DATA_SIZE((data_ring)->size_bits)
#define DATA_SIZE_MASK(data_ring)	(DATA_SIZE(data_ring) - 1)

#define DESCS_COUNT(desc_ring)		_DESCS_COUNT((desc_ring)->count_bits)
#define DESCS_COUNT_MASK(desc_ring)	(DESCS_COUNT(desc_ring) - 1)

/* Determine the data array index from a logical position. */
#define DATA_INDEX(data_ring, lpos)	((lpos) & DATA_SIZE_MASK(data_ring))

/* Determine the desc array index from an ID or sequence number. */
#define DESC_INDEX(desc_ring, n)	((n) & DESCS_COUNT_MASK(desc_ring))

/* Determine how many times the data array has wrapped. */
#define DATA_WRAPS(data_ring, lpos)	((lpos) >> (data_ring)->size_bits)

/* Determine if a logical position refers to a data-less block. */
#define LPOS_DATALESS(lpos)		((lpos) & 1UL)
#define BLK_DATALESS(blk)		(LPOS_DATALESS((blk)->begin) && \
					 LPOS_DATALESS((blk)->next))

/* Get the logical position at index 0 of the current wrap. */
#define DATA_THIS_WRAP_START_LPOS(data_ring, lpos) \
((lpos) & ~DATA_SIZE_MASK(data_ring))

/* Get the ID for the same index of the previous wrap as the given ID. */
#define DESC_ID_PREV_WRAP(desc_ring, id) \
DESC_ID((id) - DESCS_COUNT(desc_ring))

/*
 * A data block: mapped directly to the beginning of the data block area
 * specified as a logical position within the data ring.
 *
 * @id:   the ID of the associated descriptor
 * @data: the writer data
 *
 * Note that the size of a data block is only known by its associated
 * descriptor.
 */
struct prb_data_block {
	unsigned long	id;
	char		data[];
};

/*
 * Return the descriptor associated with @n. @n can be either a
 * descriptor ID or a sequence number.
 */
static struct prb_desc *to_desc(struct prb_desc_ring *desc_ring, u64 n)
{
	return &desc_ring->descs[DESC_INDEX(desc_ring, n)];
}

/*
 * Return the printk_info associated with @n. @n can be either a
 * descriptor ID or a sequence number.
 */
static struct printk_info *to_info(struct prb_desc_ring *desc_ring, u64 n)
{
	return &desc_ring->infos[DESC_INDEX(desc_ring, n)];
}

static struct prb_data_block *to_block(struct prb_data_ring *data_ring,
				       unsigned long begin_lpos)
{
	return (void *)&data_ring->data[DATA_INDEX(data_ring, begin_lpos)];
}

/*
 * Increase the data size to account for data block meta data plus any
 * padding so that the adjacent data block is aligned on the ID size.
 */
static unsigned int to_blk_size(unsigned int size)
{
	struct prb_data_block *db = NULL;

	size += sizeof(*db);
	size = ALIGN(size, sizeof(db->id));
	return size;
}

/*
 * Sanity checker for reserve size. The ringbuffer code assumes that a data
 * block does not exceed the maximum possible size that could fit within the
 * ringbuffer. This function provides that basic size check so that the
 * assumption is safe.
 */
static bool data_check_size(struct prb_data_ring *data_ring, unsigned int size)
{
	struct prb_data_block *db = NULL;

	if (size == 0)
		return true;

	/*
	 * Ensure the alignment padded size could possibly fit in the data
	 * array. The largest possible data block must still leave room for
	 * at least the ID of the next block.
	 */
	size = to_blk_size(size);
	if (size > DATA_SIZE(data_ring) - sizeof(db->id))
		return false;

	return true;
}

/* Query the state of a descriptor. */
static enum desc_state get_desc_state(unsigned long id,
				      unsigned long state_val)
{
	if (id != DESC_ID(state_val))
		return desc_miss;

	return DESC_STATE(state_val);
}

/*
 * Get a copy of a specified descriptor and return its queried state. If the
 * descriptor is in an inconsistent state (miss or reserved), the caller can
 * only expect the descriptor's @state_var field to be valid.
 *
 * The sequence number and caller_id can be optionally retrieved. Like all
 * non-state_var data, they are only valid if the descriptor is in a
 * consistent state.
 */
static enum desc_state desc_read(struct prb_desc_ring *desc_ring,
				 unsigned long id, struct prb_desc *desc_out,
				 u64 *seq_out, u32 *caller_id_out)
{
	struct printk_info *info = to_info(desc_ring, id);
	struct prb_desc *desc = to_desc(desc_ring, id);
	atomic_long_t *state_var = &desc->state_var;
	enum desc_state d_state;
	unsigned long state_val;

	/* Check the descriptor state. */
	state_val = atomic_long_read(state_var); /* LMM(desc_read:A) */
	d_state = get_desc_state(id, state_val);
	if (d_state == desc_miss || d_state == desc_reserved) {
		/*
		 * The descriptor is in an inconsistent state. Set at least
		 * @state_var so that the caller can see the details of
		 * the inconsistent state.
		 */
		goto out;
	}

	/*
	 * Guarantee the state is loaded before copying the descriptor
	 * content. This avoids copying obsolete descriptor content that might
	 * not apply to the descriptor state. This pairs with _prb_commit:B.
	 *
	 * Memory barrier involvement:
	 *
	 * If desc_read:A reads from _prb_commit:B, then desc_read:C reads
	 * from _prb_commit:A.
	 *
	 * Relies on:
	 *
	 * WMB from _prb_commit:A to _prb_commit:B
	 *    matching
	 * RMB from desc_read:A to desc_read:C
	 */
	smp_rmb(); /* LMM(desc_read:B) */

	/*
	 * Copy the descriptor data. The data is not valid until the
	 * state has been re-checked. A memcpy() for all of @desc
	 * cannot be used because of the atomic_t @state_var field.
	 */
	memcpy(&desc_out->text_blk_lpos, &desc->text_blk_lpos,
	       sizeof(desc_out->text_blk_lpos)); /* LMM(desc_read:C) */
	if (seq_out)
		*seq_out = info->seq; /* also part of desc_read:C */
	if (caller_id_out)
		*caller_id_out = info->caller_id; /* also part of desc_read:C */

	/*
	 * 1. Guarantee the descriptor content is loaded before re-checking
	 *    the state. This avoids reading an obsolete descriptor state
	 *    that may not apply to the copied content. This pairs with
	 *    desc_reserve:F.
	 *
	 *    Memory barrier involvement:
	 *
	 *    If desc_read:C reads from desc_reserve:G, then desc_read:E
	 *    reads from desc_reserve:F.
	 *
	 *    Relies on:
	 *
	 *    WMB from desc_reserve:F to desc_reserve:G
	 *       matching
	 *    RMB from desc_read:C to desc_read:E
	 *
	 * 2. Guarantee the record data is loaded before re-checking the
	 *    state. This avoids reading an obsolete descriptor state that may
	 *    not apply to the copied data. This pairs with data_alloc:A and
	 *    data_realloc:A.
	 *
	 *    Memory barrier involvement:
	 *
	 *    If copy_data:A reads from data_alloc:B, then desc_read:E
	 *    reads from desc_make_reusable:A.
	 *
	 *    Relies on:
	 *
	 *    MB from desc_make_reusable:A to data_alloc:B
	 *       matching
	 *    RMB from desc_read:C to desc_read:E
	 *
	 *    Note: desc_make_reusable:A and data_alloc:B can be different
	 *          CPUs. However, the data_alloc:B CPU (which performs the
	 *          full memory barrier) must have previously seen
	 *          desc_make_reusable:A.
	 */
	smp_rmb(); /* LMM(desc_read:D) */

	/*
	 * The data has been copied. Return the current descriptor state,
	 * which may have changed since the load above.
	 */
	state_val = atomic_long_read(state_var); /* LMM(desc_read:E) */
	d_state = get_desc_state(id, state_val);
out:
	atomic_long_set(&desc_out->state_var, state_val);
	return d_state;
}

/*
 * Take a specified descriptor out of the finalized state by attempting
 * the transition from finalized to reusable. Either this context or some
 * other context will have been successful.
 */
static void desc_make_reusable(struct prb_desc_ring *desc_ring,
			       unsigned long id)
{
	unsigned long val_finalized = DESC_SV(id, desc_finalized);
	unsigned long val_reusable = DESC_SV(id, desc_reusable);
	struct prb_desc *desc = to_desc(desc_ring, id);
	atomic_long_t *state_var = &desc->state_var;

	atomic_long_cmpxchg_relaxed(state_var, val_finalized,
				    val_reusable); /* LMM(desc_make_reusable:A) */
}

/*
 * Given the text data ring, put the associated descriptor of each
 * data block from @lpos_begin until @lpos_end into the reusable state.
 *
 * If there is any problem making the associated descriptor reusable, either
 * the descriptor has not yet been finalized or another writer context has
 * already pushed the tail lpos past the problematic data block. Regardless,
 * on error the caller can re-load the tail lpos to determine the situation.
 */
static bool data_make_reusable(struct printk_ringbuffer *rb,
			       struct prb_data_ring *data_ring,
			       unsigned long lpos_begin,
			       unsigned long lpos_end,
			       unsigned long *lpos_out)
{
	struct prb_desc_ring *desc_ring = &rb->desc_ring;
	struct prb_data_block *blk;
	enum desc_state d_state;
	struct prb_desc desc;
	struct prb_data_blk_lpos *blk_lpos = &desc.text_blk_lpos;
	unsigned long id;

	/* Loop until @lpos_begin has advanced to or beyond @lpos_end. */
	while ((lpos_end - lpos_begin) - 1 < DATA_SIZE(data_ring)) {
		blk = to_block(data_ring, lpos_begin);

		/*
		 * Load the block ID from the data block. This is a data race
		 * against a writer that may have newly reserved this data
		 * area. If the loaded value matches a valid descriptor ID,
		 * the blk_lpos of that descriptor will be checked to make
		 * sure it points back to this data block. If the check fails,
		 * the data area has been recycled by another writer.
		 */
		id = blk->id; /* LMM(data_make_reusable:A) */

		d_state = desc_read(desc_ring, id, &desc,
				    NULL, NULL); /* LMM(data_make_reusable:B) */

		switch (d_state) {
		case desc_miss:
		case desc_reserved:
		case desc_committed:
			return false;
		case desc_finalized:
			/*
			 * This data block is invalid if the descriptor
			 * does not point back to it.
			 */
			if (blk_lpos->begin != lpos_begin)
				return false;
			desc_make_reusable(desc_ring, id);
			break;
		case desc_reusable:
			/*
			 * This data block is invalid if the descriptor
			 * does not point back to it.
			 */
			if (blk_lpos->begin != lpos_begin)
				return false;
			break;
		}

		/* Advance @lpos_begin to the next data block. */
		lpos_begin = blk_lpos->next;
	}

	*lpos_out = lpos_begin;
	return true;
}

/*
 * Advance the data ring tail to at least @lpos. This function puts
 * descriptors into the reusable state if the tail is pushed beyond
 * their associated data block.
 */
static bool data_push_tail(struct printk_ringbuffer *rb,
			   struct prb_data_ring *data_ring,
			   unsigned long lpos)
{
	unsigned long tail_lpos_new;
	unsigned long tail_lpos;
	unsigned long next_lpos;

	/* If @lpos is from a data-less block, there is nothing to do. */
	if (LPOS_DATALESS(lpos))
		return true;

	/*
	 * Any descriptor states that have transitioned to reusable due to the
	 * data tail being pushed to this loaded value will be visible to this
	 * CPU. This pairs with data_push_tail:D.
	 *
	 * Memory barrier involvement:
	 *
	 * If data_push_tail:A reads from data_push_tail:D, then this CPU can
	 * see desc_make_reusable:A.
	 *
	 * Relies on:
	 *
	 * MB from desc_make_reusable:A to data_push_tail:D
	 *    matches
	 * READFROM from data_push_tail:D to data_push_tail:A
	 *    thus
	 * READFROM from desc_make_reusable:A to this CPU
	 */
	tail_lpos = atomic_long_read(&data_ring->tail_lpos); /* LMM(data_push_tail:A) */

	/*
	 * Loop until the tail lpos is at or beyond @lpos. This condition
	 * may already be satisfied, resulting in no full memory barrier
	 * from data_push_tail:D being performed. However, since this CPU
	 * sees the new tail lpos, any descriptor states that transitioned to
	 * the reusable state must already be visible.
	 */
	while ((lpos - tail_lpos) - 1 < DATA_SIZE(data_ring)) {
		/*
		 * Make all descriptors reusable that are associated with
		 * data blocks before @lpos.
		 */
		if (!data_make_reusable(rb, data_ring, tail_lpos, lpos,
					&next_lpos)) {
			/*
			 * 1. Guarantee the block ID loaded in
			 *    data_make_reusable() is performed before
			 *    reloading the tail lpos. The failed
			 *    data_make_reusable() may be due to a newly
			 *    recycled data area causing the tail lpos to
			 *    have been previously pushed. This pairs with
			 *    data_alloc:A and data_realloc:A.
			 *
			 *    Memory barrier involvement:
			 *
			 *    If data_make_reusable:A reads from data_alloc:B,
			 *    then data_push_tail:C reads from
			 *    data_push_tail:D.
			 *
			 *    Relies on:
			 *
			 *    MB from data_push_tail:D to data_alloc:B
			 *       matching
			 *    RMB from data_make_reusable:A to
			 *    data_push_tail:C
			 *
			 *    Note: data_push_tail:D and data_alloc:B can be
			 *          different CPUs. However, the data_alloc:B
			 *          CPU (which performs the full memory
			 *          barrier) must have previously seen
			 *          data_push_tail:D.
			 *
			 * 2. Guarantee the descriptor state loaded in
			 *    data_make_reusable() is performed before
			 *    reloading the tail lpos. The failed
			 *    data_make_reusable() may be due to a newly
			 *    recycled descriptor causing the tail lpos to
			 *    have been previously pushed. This pairs with
			 *    desc_reserve:D.
			 *
			 *    Memory barrier involvement:
			 *
			 *    If data_make_reusable:B reads from
			 *    desc_reserve:F, then data_push_tail:C reads
			 *    from data_push_tail:D.
			 *
			 *    Relies on:
			 *
			 *    MB from data_push_tail:D to desc_reserve:F
			 *       matching
			 *    RMB from data_make_reusable:B to
			 *    data_push_tail:C
			 *
			 *    Note: data_push_tail:D and desc_reserve:F can
			 *          be different CPUs. However, the
			 *          desc_reserve:F CPU (which performs the
			 *          full memory barrier) must have previously
			 *          seen data_push_tail:D.
			 */
			smp_rmb(); /* LMM(data_push_tail:B) */

			tail_lpos_new = atomic_long_read(&data_ring->tail_lpos
							); /* LMM(data_push_tail:C) */
			if (tail_lpos_new == tail_lpos)
				return false;

			/* Another CPU pushed the tail. Try again. */
			tail_lpos = tail_lpos_new;
			continue;
		}

		/*
		 * Guarantee any descriptor states that have transitioned to
		 * reusable are stored before pushing the tail lpos. A full
		 * memory barrier is needed since other CPUs may have made
		 * the descriptor states reusable. This pairs with
		 * data_push_tail:A.
		 */
		if (atomic_long_try_cmpxchg(&data_ring->tail_lpos, &tail_lpos,
					    next_lpos)) { /* LMM(data_push_tail:D) */
			break;
		}
	}

	return true;
}

/*
 * Advance the desc ring tail. This function advances the tail by one
 * descriptor, thus invalidating the oldest descriptor. Before advancing
 * the tail, the tail descriptor is made reusable and all data blocks up to
 * and including the descriptor's data block are invalidated (i.e. the data
 * ring tail is pushed past the data block of the descriptor being made
 * reusable).
 */
static bool desc_push_tail(struct printk_ringbuffer *rb,
			   unsigned long tail_id)
{
	struct prb_desc_ring *desc_ring = &rb->desc_ring;
	enum desc_state d_state;
	struct prb_desc desc;

	d_state = desc_read(desc_ring, tail_id, &desc, NULL, NULL);

	switch (d_state) {
	case desc_miss:
		/*
		 * If the ID is exactly 1 wrap behind the expected, it is
		 * in the process of being reserved by another writer and
		 * must be considered reserved.
		 */
		if (DESC_ID(atomic_long_read(&desc.state_var)) ==
		    DESC_ID_PREV_WRAP(desc_ring, tail_id)) {
			return false;
		}

		/*
		 * The ID has changed. Another writer must have pushed the
		 * tail and recycled the descriptor already. Success is
		 * returned because the caller is only interested in the
		 * specified tail being pushed, which it was.
		 */
		return true;
	case desc_reserved:
	case desc_committed:
		return false;
	case desc_finalized:
		desc_make_reusable(desc_ring, tail_id);
		break;
	case desc_reusable:
		break;
	}

	/*
	 * Data blocks must be invalidated before their associated
	 * descriptor can be made available for recycling. Invalidating
	 * them later is not possible because there is no way to trust
	 * data blocks once their associated descriptor is gone.
	 */

	if (!data_push_tail(rb, &rb->text_data_ring, desc.text_blk_lpos.next))
		return false;

	/*
	 * Check the next descriptor after @tail_id before pushing the tail
	 * to it because the tail must always be in a finalized or reusable
	 * state. The implementation of prb_first_seq() relies on this.
	 *
	 * A successful read implies that the next descriptor is less than or
	 * equal to @head_id so there is no risk of pushing the tail past the
	 * head.
	 */
	d_state = desc_read(desc_ring, DESC_ID(tail_id + 1), &desc,
			    NULL, NULL); /* LMM(desc_push_tail:A) */

	if (d_state == desc_finalized || d_state == desc_reusable) {
		/*
		 * Guarantee any descriptor states that have transitioned to
		 * reusable are stored before pushing the tail ID. This allows
		 * verifying the recycled descriptor state. A full memory
		 * barrier is needed since other CPUs may have made the
		 * descriptor states reusable. This pairs with desc_reserve:D.
		 */
		atomic_long_cmpxchg(&desc_ring->tail_id, tail_id,
				    DESC_ID(tail_id + 1)); /* LMM(desc_push_tail:B) */
	} else {
		/*
		 * Guarantee the last state load from desc_read() is before
		 * reloading @tail_id in order to see a new tail ID in the
		 * case that the descriptor has been recycled. This pairs
		 * with desc_reserve:D.
		 *
		 * Memory barrier involvement:
		 *
		 * If desc_push_tail:A reads from desc_reserve:F, then
		 * desc_push_tail:D reads from desc_push_tail:B.
		 *
		 * Relies on:
		 *
		 * MB from desc_push_tail:B to desc_reserve:F
		 *    matching
		 * RMB from desc_push_tail:A to desc_push_tail:D
		 *
		 * Note: desc_push_tail:B and desc_reserve:F can be different
		 *       CPUs. However, the desc_reserve:F CPU (which performs
		 *       the full memory barrier) must have previously seen
		 *       desc_push_tail:B.
		 */
		smp_rmb(); /* LMM(desc_push_tail:C) */

		/*
		 * Re-check the tail ID. The descriptor following @tail_id is
		 * not in an allowed tail state. But if the tail has since
		 * been moved by another CPU, then it does not matter.
		 */
		if (atomic_long_read(&desc_ring->tail_id) == tail_id) /* LMM(desc_push_tail:D) */
			return false;
	}

	return true;
}

/* Reserve a new descriptor, invalidating the oldest if necessary. */
static bool desc_reserve(struct printk_ringbuffer *rb, unsigned long *id_out)
{
	struct prb_desc_ring *desc_ring = &rb->desc_ring;
	unsigned long prev_state_val;
	unsigned long id_prev_wrap;
	struct prb_desc *desc;
	unsigned long head_id;
	unsigned long id;

	head_id = atomic_long_read(&desc_ring->head_id); /* LMM(desc_reserve:A) */

	do {
		id = DESC_ID(head_id + 1);
		id_prev_wrap = DESC_ID_PREV_WRAP(desc_ring, id);

		/*
		 * Guarantee the head ID is read before reading the tail ID.
		 * Since the tail ID is updated before the head ID, this
		 * guarantees that @id_prev_wrap is never ahead of the tail
		 * ID. This pairs with desc_reserve:D.
		 *
		 * Memory barrier involvement:
		 *
		 * If desc_reserve:A reads from desc_reserve:D, then
		 * desc_reserve:C reads from desc_push_tail:B.
		 *
		 * Relies on:
		 *
		 * MB from desc_push_tail:B to desc_reserve:D
		 *    matching
		 * RMB from desc_reserve:A to desc_reserve:C
		 *
		 * Note: desc_push_tail:B and desc_reserve:D can be different
		 *       CPUs. However, the desc_reserve:D CPU (which performs
		 *       the full memory barrier) must have previously seen
		 *       desc_push_tail:B.
		 */
		smp_rmb(); /* LMM(desc_reserve:B) */

		if (id_prev_wrap == atomic_long_read(&desc_ring->tail_id
						    )) { /* LMM(desc_reserve:C) */
			/*
			 * Make space for the new descriptor by
			 * advancing the tail.
			 */
			if (!desc_push_tail(rb, id_prev_wrap))
				return false;
		}

		/*
		 * 1. Guarantee the tail ID is read before validating the
		 *    recycled descriptor state. A read memory barrier is
		 *    sufficient for this. This pairs with desc_push_tail:B.
		 *
		 *    Memory barrier involvement:
		 *
		 *    If desc_reserve:C reads from desc_push_tail:B, then
		 *    desc_reserve:E reads from desc_make_reusable:A.
		 *
		 *    Relies on:
		 *
		 *    MB from desc_make_reusable:A to desc_push_tail:B
		 *       matching
		 *    RMB from desc_reserve:C to desc_reserve:E
		 *
		 *    Note: desc_make_reusable:A and desc_push_tail:B can be
		 *          different CPUs. However, the desc_push_tail:B CPU
		 *          (which performs the full memory barrier) must have
		 *          previously seen desc_make_reusable:A.
		 *
		 * 2. Guarantee the tail ID is stored before storing the head
		 *    ID. This pairs with desc_reserve:B.
		 *
		 * 3. Guarantee any data ring tail changes are stored before
		 *    recycling the descriptor. Data ring tail changes can
		 *    happen via desc_push_tail()->data_push_tail(). A full
		 *    memory barrier is needed since another CPU may have
		 *    pushed the data ring tails. This pairs with
		 *    data_push_tail:B.
		 *
		 * 4. Guarantee a new tail ID is stored before recycling the
		 *    descriptor. A full memory barrier is needed since
		 *    another CPU may have pushed the tail ID. This pairs
		 *    with desc_push_tail:C and this also pairs with
		 *    prb_first_seq:C.
		 *
		 * 5. Guarantee the head ID is stored before trying to
		 *    finalize the previous descriptor. This pairs with
		 *    _prb_commit:B.
		 */
	} while (!atomic_long_try_cmpxchg(&desc_ring->head_id, &head_id,
					  id)); /* LMM(desc_reserve:D) */

	desc = to_desc(desc_ring, id);

	/*
	 * If the descriptor has been recycled, verify the old state val.
	 * See "ABA Issues" about why this verification is performed.
	 */
	prev_state_val = atomic_long_read(&desc->state_var); /* LMM(desc_reserve:E) */
	if (prev_state_val &&
	    get_desc_state(id_prev_wrap, prev_state_val) != desc_reusable) {
		WARN_ON_ONCE(1);
		return false;
	}

	/*
	 * Assign the descriptor a new ID and set its state to reserved.
	 * See "ABA Issues" about why cmpxchg() instead of set() is used.
	 *
	 * Guarantee the new descriptor ID and state is stored before making
	 * any other changes. A write memory barrier is sufficient for this.
	 * This pairs with desc_read:D.
	 */
	if (!atomic_long_try_cmpxchg(&desc->state_var, &prev_state_val,
			DESC_SV(id, desc_reserved))) { /* LMM(desc_reserve:F) */
		WARN_ON_ONCE(1);
		return false;
	}

	/* Now data in @desc can be modified: LMM(desc_reserve:G) */

	*id_out = id;
	return true;
}

/* Determine the end of a data block. */
static unsigned long get_next_lpos(struct prb_data_ring *data_ring,
				   unsigned long lpos, unsigned int size)
{
	unsigned long begin_lpos;
	unsigned long next_lpos;

	begin_lpos = lpos;
	next_lpos = lpos + size;

	/* First check if the data block does not wrap. */
	if (DATA_WRAPS(data_ring, begin_lpos) == DATA_WRAPS(data_ring, next_lpos))
		return next_lpos;

	/* Wrapping data blocks store their data at the beginning. */
	return (DATA_THIS_WRAP_START_LPOS(data_ring, next_lpos) + size);
}

/*
 * Allocate a new data block, invalidating the oldest data block(s)
 * if necessary. This function also associates the data block with
 * a specified descriptor.
 */
static char *data_alloc(struct printk_ringbuffer *rb,
			struct prb_data_ring *data_ring, unsigned int size,
			struct prb_data_blk_lpos *blk_lpos, unsigned long id)
{
	struct prb_data_block *blk;
	unsigned long begin_lpos;
	unsigned long next_lpos;

	if (size == 0) {
		/* Specify a data-less block. */
		blk_lpos->begin = NO_LPOS;
		blk_lpos->next = NO_LPOS;
		return NULL;
	}

	size = to_blk_size(size);

	begin_lpos = atomic_long_read(&data_ring->head_lpos);

	do {
		next_lpos = get_next_lpos(data_ring, begin_lpos, size);

		if (!data_push_tail(rb, data_ring, next_lpos - DATA_SIZE(data_ring))) {
			/* Failed to allocate, specify a data-less block. */
			blk_lpos->begin = FAILED_LPOS;
			blk_lpos->next = FAILED_LPOS;
			return NULL;
		}

		/*
		 * 1. Guarantee any descriptor states that have transitioned
		 *    to reusable are stored before modifying the newly
		 *    allocated data area. A full memory barrier is needed
		 *    since other CPUs may have made the descriptor states
		 *    reusable. See data_push_tail:A about why the reusable
		 *    states are visible. This pairs with desc_read:D.
		 *
		 * 2. Guarantee any updated tail lpos is stored before
		 *    modifying the newly allocated data area. Another CPU may
		 *    be in data_make_reusable() and is reading a block ID
		 *    from this area. data_make_reusable() can handle reading
		 *    a garbage block ID value, but then it must be able to
		 *    load a new tail lpos. A full memory barrier is needed
		 *    since other CPUs may have updated the tail lpos. This
		 *    pairs with data_push_tail:B.
		 */
	} while (!atomic_long_try_cmpxchg(&data_ring->head_lpos, &begin_lpos,
					  next_lpos)); /* LMM(data_alloc:A) */

	blk = to_block(data_ring, begin_lpos);
	blk->id = id; /* LMM(data_alloc:B) */

	if (DATA_WRAPS(data_ring, begin_lpos) != DATA_WRAPS(data_ring, next_lpos)) {
		/* Wrapping data blocks store their data at the beginning. */
		blk = to_block(data_ring, 0);

		/*
		 * Store the ID on the wrapped block for consistency.
		 * The printk_ringbuffer does not actually use it.
		 */
		blk->id = id;
	}

	blk_lpos->begin = begin_lpos;
	blk_lpos->next = next_lpos;

	return &blk->data[0];
}

/*
 * Try to resize an existing data block associated with the descriptor
 * specified by @id. If the resized data block should become wrapped, it
 * copies the old data to the new data block. If @size yields a data block
 * with the same or less size, the data block is left as is.
 *
 * Fail if this is not the last allocated data block or if there is not
 * enough space or it is not possible make enough space.
 *
 * Return a pointer to the beginning of the entire data buffer or NULL on
 * failure.
 */
static char *data_realloc(struct printk_ringbuffer *rb,
			  struct prb_data_ring *data_ring, unsigned int size,
			  struct prb_data_blk_lpos *blk_lpos, unsigned long id)
{
	struct prb_data_block *blk;
	unsigned long head_lpos;
	unsigned long next_lpos;
	bool wrapped;

	/* Reallocation only works if @blk_lpos is the newest data block. */
	head_lpos = atomic_long_read(&data_ring->head_lpos);
	if (head_lpos != blk_lpos->next)
		return NULL;

	/* Keep track if @blk_lpos was a wrapping data block. */
	wrapped = (DATA_WRAPS(data_ring, blk_lpos->begin) != DATA_WRAPS(data_ring, blk_lpos->next));

	size = to_blk_size(size);

	next_lpos = get_next_lpos(data_ring, blk_lpos->begin, size);

	/* If the data block does not increase, there is nothing to do. */
	if (head_lpos - next_lpos < DATA_SIZE(data_ring)) {
		if (wrapped)
			blk = to_block(data_ring, 0);
		else
			blk = to_block(data_ring, blk_lpos->begin);
		return &blk->data[0];
	}

	if (!data_push_tail(rb, data_ring, next_lpos - DATA_SIZE(data_ring)))
		return NULL;

	/* The memory barrier involvement is the same as data_alloc:A. */
	if (!atomic_long_try_cmpxchg(&data_ring->head_lpos, &head_lpos,
				     next_lpos)) { /* LMM(data_realloc:A) */
		return NULL;
	}

	blk = to_block(data_ring, blk_lpos->begin);

	if (DATA_WRAPS(data_ring, blk_lpos->begin) != DATA_WRAPS(data_ring, next_lpos)) {
		struct prb_data_block *old_blk = blk;

		/* Wrapping data blocks store their data at the beginning. */
		blk = to_block(data_ring, 0);

		/*
		 * Store the ID on the wrapped block for consistency.
		 * The printk_ringbuffer does not actually use it.
		 */
		blk->id = id;

		if (!wrapped) {
			/*
			 * Since the allocated space is now in the newly
			 * created wrapping data block, copy the content
			 * from the old data block.
			 */
			memcpy(&blk->data[0], &old_blk->data[0],
			       (blk_lpos->next - blk_lpos->begin) - sizeof(blk->id));
		}
	}

	blk_lpos->next = next_lpos;

	return &blk->data[0];
}

/* Return the number of bytes used by a data block. */
static unsigned int space_used(struct prb_data_ring *data_ring,
			       struct prb_data_blk_lpos *blk_lpos)
{
	/* Data-less blocks take no space. */
	if (BLK_DATALESS(blk_lpos))
		return 0;

	if (DATA_WRAPS(data_ring, blk_lpos->begin) == DATA_WRAPS(data_ring, blk_lpos->next)) {
		/* Data block does not wrap. */
		return (DATA_INDEX(data_ring, blk_lpos->next) -
			DATA_INDEX(data_ring, blk_lpos->begin));
	}

	/*
	 * For wrapping data blocks, the trailing (wasted) space is
	 * also counted.
	 */
	return (DATA_INDEX(data_ring, blk_lpos->next) +
		DATA_SIZE(data_ring) - DATA_INDEX(data_ring, blk_lpos->begin));
}

/*
 * Given @blk_lpos, return a pointer to the writer data from the data block
 * and calculate the size of the data part. A NULL pointer is returned if
 * @blk_lpos specifies values that could never be legal.
 *
 * This function (used by readers) performs strict validation on the lpos
 * values to possibly detect bugs in the writer code. A WARN_ON_ONCE() is
 * triggered if an internal error is detected.
 */
static const char *get_data(struct prb_data_ring *data_ring,
			    struct prb_data_blk_lpos *blk_lpos,
			    unsigned int *data_size)
{
	struct prb_data_block *db;

	/* Data-less data block description. */
	if (BLK_DATALESS(blk_lpos)) {
		if (blk_lpos->begin == NO_LPOS && blk_lpos->next == NO_LPOS) {
			*data_size = 0;
			return "";
		}
		return NULL;
	}

	/* Regular data block: @begin less than @next and in same wrap. */
	if (DATA_WRAPS(data_ring, blk_lpos->begin) == DATA_WRAPS(data_ring, blk_lpos->next) &&
	    blk_lpos->begin < blk_lpos->next) {
		db = to_block(data_ring, blk_lpos->begin);
		*data_size = blk_lpos->next - blk_lpos->begin;

	/* Wrapping data block: @begin is one wrap behind @next. */
	} else if (DATA_WRAPS(data_ring, blk_lpos->begin + DATA_SIZE(data_ring)) ==
		   DATA_WRAPS(data_ring, blk_lpos->next)) {
		db = to_block(data_ring, 0);
		*data_size = DATA_INDEX(data_ring, blk_lpos->next);

	/* Illegal block description. */
	} else {
		WARN_ON_ONCE(1);
		return NULL;
	}

	/* A valid data block will always be aligned to the ID size. */
	if (WARN_ON_ONCE(blk_lpos->begin != ALIGN(blk_lpos->begin, sizeof(db->id))) ||
	    WARN_ON_ONCE(blk_lpos->next != ALIGN(blk_lpos->next, sizeof(db->id)))) {
		return NULL;
	}

	/* A valid data block will always have at least an ID. */
	if (WARN_ON_ONCE(*data_size < sizeof(db->id)))
		return NULL;

	/* Subtract block ID space from size to reflect data size. */
	*data_size -= sizeof(db->id);

	return &db->data[0];
}

/*
 * Attempt to transition the newest descriptor from committed back to reserved
 * so that the record can be modified by a writer again. This is only possible
 * if the descriptor is not yet finalized and the provided @caller_id matches.
 */
static struct prb_desc *desc_reopen_last(struct prb_desc_ring *desc_ring,
					 u32 caller_id, unsigned long *id_out)
{
	unsigned long prev_state_val;
	enum desc_state d_state;
	struct prb_desc desc;
	struct prb_desc *d;
	unsigned long id;
	u32 cid;

	id = atomic_long_read(&desc_ring->head_id);

	/*
	 * To reduce unnecessarily reopening, first check if the descriptor
	 * state and caller ID are correct.
	 */
	d_state = desc_read(desc_ring, id, &desc, NULL, &cid);
	if (d_state != desc_committed || cid != caller_id)
		return NULL;

	d = to_desc(desc_ring, id);

	prev_state_val = DESC_SV(id, desc_committed);

	/*
	 * Guarantee the reserved state is stored before reading any
	 * record data. A full memory barrier is needed because @state_var
	 * modification is followed by reading. This pairs with _prb_commit:B.
	 *
	 * Memory barrier involvement:
	 *
	 * If desc_reopen_last:A reads from _prb_commit:B, then
	 * prb_reserve_in_last:A reads from _prb_commit:A.
	 *
	 * Relies on:
	 *
	 * WMB from _prb_commit:A to _prb_commit:B
	 *    matching
	 * MB If desc_reopen_last:A to prb_reserve_in_last:A
	 */
	if (!atomic_long_try_cmpxchg(&d->state_var, &prev_state_val,
			DESC_SV(id, desc_reserved))) { /* LMM(desc_reopen_last:A) */
		return NULL;
	}

	*id_out = id;
	return d;
}

/**
 * prb_reserve_in_last() - Re-reserve and extend the space in the ringbuffer
 *                         used by the newest record.
 *
 * @e:         The entry structure to setup.
 * @rb:        The ringbuffer to re-reserve and extend data in.
 * @r:         The record structure to allocate buffers for.
 * @caller_id: The caller ID of the caller (reserving writer).
 * @max_size:  Fail if the extended size would be greater than this.
 *
 * This is the public function available to writers to re-reserve and extend
 * data.
 *
 * The writer specifies the text size to extend (not the new total size) by
 * setting the @text_buf_size field of @r. To ensure proper initialization
 * of @r, prb_rec_init_wr() should be used.
 *
 * This function will fail if @caller_id does not match the caller ID of the
 * newest record. In that case the caller must reserve new data using
 * prb_reserve().
 *
 * Context: Any context. Disables local interrupts on success.
 * Return: true if text data could be extended, otherwise false.
 *
 * On success:
 *
 *   - @r->text_buf points to the beginning of the entire text buffer.
 *
 *   - @r->text_buf_size is set to the new total size of the buffer.
 *
 *   - @r->info is not touched so that @r->info->text_len could be used
 *     to append the text.
 *
 *   - prb_record_text_space() can be used on @e to query the new
 *     actually used space.
 *
 * Important: All @r->info fields will already be set with the current values
 *            for the record. I.e. @r->info->text_len will be less than
 *            @text_buf_size. Writers can use @r->info->text_len to know
 *            where concatenation begins and writers should update
 *            @r->info->text_len after concatenating.
 */
bool prb_reserve_in_last(struct prb_reserved_entry *e, struct printk_ringbuffer *rb,
			 struct printk_record *r, u32 caller_id, unsigned int max_size)
{
	struct prb_desc_ring *desc_ring = &rb->desc_ring;
	struct printk_info *info;
	unsigned int data_size;
	struct prb_desc *d;
	unsigned long id;

	local_irq_save(e->irqflags);

	/* Transition the newest descriptor back to the reserved state. */
	d = desc_reopen_last(desc_ring, caller_id, &id);
	if (!d) {
		local_irq_restore(e->irqflags);
		goto fail_reopen;
	}

	/* Now the writer has exclusive access: LMM(prb_reserve_in_last:A) */

	info = to_info(desc_ring, id);

	/*
	 * Set the @e fields here so that prb_commit() can be used if
	 * anything fails from now on.
	 */
	e->rb = rb;
	e->id = id;

	/*
	 * desc_reopen_last() checked the caller_id, but there was no
	 * exclusive access at that point. The descriptor may have
	 * changed since then.
	 */
	if (caller_id != info->caller_id)
		goto fail;

	if (BLK_DATALESS(&d->text_blk_lpos)) {
		if (WARN_ON_ONCE(info->text_len != 0)) {
			pr_warn_once("wrong text_len value (%hu, expecting 0)\n",
				     info->text_len);
			info->text_len = 0;
		}

		if (!data_check_size(&rb->text_data_ring, r->text_buf_size))
			goto fail;

		if (r->text_buf_size > max_size)
			goto fail;

		r->text_buf = data_alloc(rb, &rb->text_data_ring, r->text_buf_size,
					 &d->text_blk_lpos, id);
	} else {
		if (!get_data(&rb->text_data_ring, &d->text_blk_lpos, &data_size))
			goto fail;

		/*
		 * Increase the buffer size to include the original size. If
		 * the meta data (@text_len) is not sane, use the full data
		 * block size.
		 */
		if (WARN_ON_ONCE(info->text_len > data_size)) {
			pr_warn_once("wrong text_len value (%hu, expecting <=%u)\n",
				     info->text_len, data_size);
			info->text_len = data_size;
		}
		r->text_buf_size += info->text_len;

		if (!data_check_size(&rb->text_data_ring, r->text_buf_size))
			goto fail;

		if (r->text_buf_size > max_size)
			goto fail;

		r->text_buf = data_realloc(rb, &rb->text_data_ring, r->text_buf_size,
					   &d->text_blk_lpos, id);
	}
	if (r->text_buf_size && !r->text_buf)
		goto fail;

	r->info = info;

	e->text_space = space_used(&rb->text_data_ring, &d->text_blk_lpos);

	return true;
fail:
	prb_commit(e);
	/* prb_commit() re-enabled interrupts. */
fail_reopen:
	/* Make it clear to the caller that the re-reserve failed. */
	memset(r, 0, sizeof(*r));
	return false;
}

/*
 * Attempt to finalize a specified descriptor. If this fails, the descriptor
 * is either already final or it will finalize itself when the writer commits.
 */
static void desc_make_final(struct prb_desc_ring *desc_ring, unsigned long id)
{
	unsigned long prev_state_val = DESC_SV(id, desc_committed);
	struct prb_desc *d = to_desc(desc_ring, id);

	atomic_long_cmpxchg_relaxed(&d->state_var, prev_state_val,
			DESC_SV(id, desc_finalized)); /* LMM(desc_make_final:A) */
}

/**
 * prb_reserve() - Reserve space in the ringbuffer.
 *
 * @e:  The entry structure to setup.
 * @rb: The ringbuffer to reserve data in.
 * @r:  The record structure to allocate buffers for.
 *
 * This is the public function available to writers to reserve data.
 *
 * The writer specifies the text size to reserve by setting the
 * @text_buf_size field of @r. To ensure proper initialization of @r,
 * prb_rec_init_wr() should be used.
 *
 * Context: Any context. Disables local interrupts on success.
 * Return: true if at least text data could be allocated, otherwise false.
 *
 * On success, the fields @info and @text_buf of @r will be set by this
 * function and should be filled in by the writer before committing. Also
 * on success, prb_record_text_space() can be used on @e to query the actual
 * space used for the text data block.
 *
 * Important: @info->text_len needs to be set correctly by the writer in
 *            order for data to be readable and/or extended. Its value
 *            is initialized to 0.
 */
bool prb_reserve(struct prb_reserved_entry *e, struct printk_ringbuffer *rb,
		 struct printk_record *r)
{
	struct prb_desc_ring *desc_ring = &rb->desc_ring;
	struct printk_info *info;
	struct prb_desc *d;
	unsigned long id;
	u64 seq;

	if (!data_check_size(&rb->text_data_ring, r->text_buf_size))
		goto fail;

	/*
	 * Descriptors in the reserved state act as blockers to all further
	 * reservations once the desc_ring has fully wrapped. Disable
	 * interrupts during the reserve/commit window in order to minimize
	 * the likelihood of this happening.
	 */
	local_irq_save(e->irqflags);

	if (!desc_reserve(rb, &id)) {
		/* Descriptor reservation failures are tracked. */
		atomic_long_inc(&rb->fail);
		local_irq_restore(e->irqflags);
		goto fail;
	}

	d = to_desc(desc_ring, id);
	info = to_info(desc_ring, id);

	/*
	 * All @info fields (except @seq) are cleared and must be filled in
	 * by the writer. Save @seq before clearing because it is used to
	 * determine the new sequence number.
	 */
	seq = info->seq;
	memset(info, 0, sizeof(*info));

	/*
	 * Set the @e fields here so that prb_commit() can be used if
	 * text data allocation fails.
	 */
	e->rb = rb;
	e->id = id;

	/*
	 * Initialize the sequence number if it has "never been set".
	 * Otherwise just increment it by a full wrap.
	 *
	 * @seq is considered "never been set" if it has a value of 0,
	 * _except_ for @infos[0], which was specially setup by the ringbuffer
	 * initializer and therefore is always considered as set.
	 *
	 * See the "Bootstrap" comment block in printk_ringbuffer.h for
	 * details about how the initializer bootstraps the descriptors.
	 */
	if (seq == 0 && DESC_INDEX(desc_ring, id) != 0)
		info->seq = DESC_INDEX(desc_ring, id);
	else
		info->seq = seq + DESCS_COUNT(desc_ring);

	/*
	 * New data is about to be reserved. Once that happens, previous
	 * descriptors are no longer able to be extended. Finalize the
	 * previous descriptor now so that it can be made available to
	 * readers. (For seq==0 there is no previous descriptor.)
	 */
	if (info->seq > 0)
		desc_make_final(desc_ring, DESC_ID(id - 1));

	r->text_buf = data_alloc(rb, &rb->text_data_ring, r->text_buf_size,
				 &d->text_blk_lpos, id);
	/* If text data allocation fails, a data-less record is committed. */
	if (r->text_buf_size && !r->text_buf) {
		prb_commit(e);
		/* prb_commit() re-enabled interrupts. */
		goto fail;
	}

	r->info = info;

	/* Record full text space used by record. */
	e->text_space = space_used(&rb->text_data_ring, &d->text_blk_lpos);

	return true;
fail:
	/* Make it clear to the caller that the reserve failed. */
	memset(r, 0, sizeof(*r));
	return false;
}

/* Commit the data (possibly finalizing it) and restore interrupts. */
static void _prb_commit(struct prb_reserved_entry *e, unsigned long state_val)
{
	struct prb_desc_ring *desc_ring = &e->rb->desc_ring;
	struct prb_desc *d = to_desc(desc_ring, e->id);
	unsigned long prev_state_val = DESC_SV(e->id, desc_reserved);

	/* Now the writer has finished all writing: LMM(_prb_commit:A) */

	/*
	 * Set the descriptor as committed. See "ABA Issues" about why
	 * cmpxchg() instead of set() is used.
	 *
	 * 1  Guarantee all record data is stored before the descriptor state
	 *    is stored as committed. A write memory barrier is sufficient
	 *    for this. This pairs with desc_read:B and desc_reopen_last:A.
	 *
	 * 2. Guarantee the descriptor state is stored as committed before
	 *    re-checking the head ID in order to possibly finalize this
	 *    descriptor. This pairs with desc_reserve:D.
	 *
	 *    Memory barrier involvement:
	 *
	 *    If prb_commit:A reads from desc_reserve:D, then
	 *    desc_make_final:A reads from _prb_commit:B.
	 *
	 *    Relies on:
	 *
	 *    MB _prb_commit:B to prb_commit:A
	 *       matching
	 *    MB desc_reserve:D to desc_make_final:A
	 */
	if (!atomic_long_try_cmpxchg(&d->state_var, &prev_state_val,
			DESC_SV(e->id, state_val))) { /* LMM(_prb_commit:B) */
		WARN_ON_ONCE(1);
	}

	/* Restore interrupts, the reserve/commit window is finished. */
	local_irq_restore(e->irqflags);
}

/**
 * prb_commit() - Commit (previously reserved) data to the ringbuffer.
 *
 * @e: The entry containing the reserved data information.
 *
 * This is the public function available to writers to commit data.
 *
 * Note that the data is not yet available to readers until it is finalized.
 * Finalizing happens automatically when space for the next record is
 * reserved.
 *
 * See prb_final_commit() for a version of this function that finalizes
 * immediately.
 *
 * Context: Any context. Enables local interrupts.
 */
void prb_commit(struct prb_reserved_entry *e)
{
	struct prb_desc_ring *desc_ring = &e->rb->desc_ring;
	unsigned long head_id;

	_prb_commit(e, desc_committed);

	/*
	 * If this descriptor is no longer the head (i.e. a new record has
	 * been allocated), extending the data for this record is no longer
	 * allowed and therefore it must be finalized.
	 */
	head_id = atomic_long_read(&desc_ring->head_id); /* LMM(prb_commit:A) */
	if (head_id != e->id)
		desc_make_final(desc_ring, e->id);
}

/**
 * prb_final_commit() - Commit and finalize (previously reserved) data to
 *                      the ringbuffer.
 *
 * @e: The entry containing the reserved data information.
 *
 * This is the public function available to writers to commit+finalize data.
 *
 * By finalizing, the data is made immediately available to readers.
 *
 * This function should only be used if there are no intentions of extending
 * this data using prb_reserve_in_last().
 *
 * Context: Any context. Enables local interrupts.
 */
void prb_final_commit(struct prb_reserved_entry *e)
{
	_prb_commit(e, desc_finalized);
}

/*
 * Count the number of lines in provided text. All text has at least 1 line
 * (even if @text_size is 0). Each '\n' processed is counted as an additional
 * line.
 */
static unsigned int count_lines(const char *text, unsigned int text_size)
{
	unsigned int next_size = text_size;
	unsigned int line_count = 1;
	const char *next = text;

	while (next_size) {
		next = memchr(next, '\n', next_size);
		if (!next)
			break;
		line_count++;
		next++;
		next_size = text_size - (next - text);
	}

	return line_count;
}

/*
 * Given @blk_lpos, copy an expected @len of data into the provided buffer.
 * If @line_count is provided, count the number of lines in the data.
 *
 * This function (used by readers) performs strict validation on the data
 * size to possibly detect bugs in the writer code. A WARN_ON_ONCE() is
 * triggered if an internal error is detected.
 */
static bool copy_data(struct prb_data_ring *data_ring,
		      struct prb_data_blk_lpos *blk_lpos, u16 len, char *buf,
		      unsigned int buf_size, unsigned int *line_count)
{
	unsigned int data_size;
	const char *data;

	/* Caller might not want any data. */
	if ((!buf || !buf_size) && !line_count)
		return true;

	data = get_data(data_ring, blk_lpos, &data_size);
	if (!data)
		return false;

	/*
	 * Actual cannot be less than expected. It can be more than expected
	 * because of the trailing alignment padding.
	 *
	 * Note that invalid @len values can occur because the caller loads
	 * the value during an allowed data race.
	 */
	if (data_size < (unsigned int)len)
		return false;

	/* Caller interested in the line count? */
	if (line_count)
		*line_count = count_lines(data, len);

	/* Caller interested in the data content? */
	if (!buf || !buf_size)
		return true;

	data_size = min_t(u16, buf_size, len);

	memcpy(&buf[0], data, data_size); /* LMM(copy_data:A) */
	return true;
}

/*
 * This is an extended version of desc_read(). It gets a copy of a specified
 * descriptor. However, it also verifies that the record is finalized and has
 * the sequence number @seq. On success, 0 is returned.
 *
 * Error return values:
 * -EINVAL: A finalized record with sequence number @seq does not exist.
 * -ENOENT: A finalized record with sequence number @seq exists, but its data
 *          is not available. This is a valid record, so readers should
 *          continue with the next record.
 */
static int desc_read_finalized_seq(struct prb_desc_ring *desc_ring,
				   unsigned long id, u64 seq,
				   struct prb_desc *desc_out)
{
	struct prb_data_blk_lpos *blk_lpos = &desc_out->text_blk_lpos;
	enum desc_state d_state;
	u64 s;

	d_state = desc_read(desc_ring, id, desc_out, &s, NULL);

	/*
	 * An unexpected @id (desc_miss) or @seq mismatch means the record
	 * does not exist. A descriptor in the reserved or committed state
	 * means the record does not yet exist for the reader.
	 */
	if (d_state == desc_miss ||
	    d_state == desc_reserved ||
	    d_state == desc_committed ||
	    s != seq) {
		return -EINVAL;
	}

	/*
	 * A descriptor in the reusable state may no longer have its data
	 * available; report it as existing but with lost data. Or the record
	 * may actually be a record with lost data.
	 */
	if (d_state == desc_reusable ||
	    (blk_lpos->begin == FAILED_LPOS && blk_lpos->next == FAILED_LPOS)) {
		return -ENOENT;
	}

	return 0;
}

/*
 * Copy the ringbuffer data from the record with @seq to the provided
 * @r buffer. On success, 0 is returned.
 *
 * See desc_read_finalized_seq() for error return values.
 */
static int prb_read(struct printk_ringbuffer *rb, u64 seq,
		    struct printk_record *r, unsigned int *line_count)
{
	struct prb_desc_ring *desc_ring = &rb->desc_ring;
	struct printk_info *info = to_info(desc_ring, seq);
	struct prb_desc *rdesc = to_desc(desc_ring, seq);
	atomic_long_t *state_var = &rdesc->state_var;
	struct prb_desc desc;
	unsigned long id;
	int err;

	/* Extract the ID, used to specify the descriptor to read. */
	id = DESC_ID(atomic_long_read(state_var));

	/* Get a local copy of the correct descriptor (if available). */
	err = desc_read_finalized_seq(desc_ring, id, seq, &desc);

	/*
	 * If @r is NULL, the caller is only interested in the availability
	 * of the record.
	 */
	if (err || !r)
		return err;

	/* If requested, copy meta data. */
	if (r->info)
		memcpy(r->info, info, sizeof(*(r->info)));

	/* Copy text data. If it fails, this is a data-less record. */
	if (!copy_data(&rb->text_data_ring, &desc.text_blk_lpos, info->text_len,
		       r->text_buf, r->text_buf_size, line_count)) {
		return -ENOENT;
	}

	/* Ensure the record is still finalized and has the same @seq. */
	return desc_read_finalized_seq(desc_ring, id, seq, &desc);
}

/* Get the sequence number of the tail descriptor. */
static u64 prb_first_seq(struct printk_ringbuffer *rb)
{
	struct prb_desc_ring *desc_ring = &rb->desc_ring;
	enum desc_state d_state;
	struct prb_desc desc;
	unsigned long id;
	u64 seq;

	for (;;) {
		id = atomic_long_read(&rb->desc_ring.tail_id); /* LMM(prb_first_seq:A) */

		d_state = desc_read(desc_ring, id, &desc, &seq, NULL); /* LMM(prb_first_seq:B) */

		/*
		 * This loop will not be infinite because the tail is
		 * _always_ in the finalized or reusable state.
		 */
		if (d_state == desc_finalized || d_state == desc_reusable)
			break;

		/*
		 * Guarantee the last state load from desc_read() is before
		 * reloading @tail_id in order to see a new tail in the case
		 * that the descriptor has been recycled. This pairs with
		 * desc_reserve:D.
		 *
		 * Memory barrier involvement:
		 *
		 * If prb_first_seq:B reads from desc_reserve:F, then
		 * prb_first_seq:A reads from desc_push_tail:B.
		 *
		 * Relies on:
		 *
		 * MB from desc_push_tail:B to desc_reserve:F
		 *    matching
		 * RMB prb_first_seq:B to prb_first_seq:A
		 */
		smp_rmb(); /* LMM(prb_first_seq:C) */
	}

	return seq;
}

/*
 * Non-blocking read of a record. Updates @seq to the last finalized record
 * (which may have no data available).
 *
 * See the description of prb_read_valid() and prb_read_valid_info()
 * for details.
 */
static bool _prb_read_valid(struct printk_ringbuffer *rb, u64 *seq,
			    struct printk_record *r, unsigned int *line_count)
{
	u64 tail_seq;
	int err;

	while ((err = prb_read(rb, *seq, r, line_count))) {
		tail_seq = prb_first_seq(rb);

		if (*seq < tail_seq) {
			/*
			 * Behind the tail. Catch up and try again. This
			 * can happen for -ENOENT and -EINVAL cases.
			 */
			*seq = tail_seq;

		} else if (err == -ENOENT) {
			/* Record exists, but no data available. Skip. */
			(*seq)++;

		} else {
			/* Non-existent/non-finalized record. Must stop. */
			return false;
		}
	}

	return true;
}

/**
 * prb_read_valid() - Non-blocking read of a requested record or (if gone)
 *                    the next available record.
 *
 * @rb:  The ringbuffer to read from.
 * @seq: The sequence number of the record to read.
 * @r:   A record data buffer to store the read record to.
 *
 * This is the public function available to readers to read a record.
 *
 * The reader provides the @info and @text_buf buffers of @r to be
 * filled in. Any of the buffer pointers can be set to NULL if the reader
 * is not interested in that data. To ensure proper initialization of @r,
 * prb_rec_init_rd() should be used.
 *
 * Context: Any context.
 * Return: true if a record was read, otherwise false.
 *
 * On success, the reader must check r->info.seq to see which record was
 * actually read. This allows the reader to detect dropped records.
 *
 * Failure means @seq refers to a not yet written record.
 */
bool prb_read_valid(struct printk_ringbuffer *rb, u64 seq,
		    struct printk_record *r)
{
	return _prb_read_valid(rb, &seq, r, NULL);
}

/**
 * prb_read_valid_info() - Non-blocking read of meta data for a requested
 *                         record or (if gone) the next available record.
 *
 * @rb:         The ringbuffer to read from.
 * @seq:        The sequence number of the record to read.
 * @info:       A buffer to store the read record meta data to.
 * @line_count: A buffer to store the number of lines in the record text.
 *
 * This is the public function available to readers to read only the
 * meta data of a record.
 *
 * The reader provides the @info, @line_count buffers to be filled in.
 * Either of the buffer pointers can be set to NULL if the reader is not
 * interested in that data.
 *
 * Context: Any context.
 * Return: true if a record's meta data was read, otherwise false.
 *
 * On success, the reader must check info->seq to see which record meta data
 * was actually read. This allows the reader to detect dropped records.
 *
 * Failure means @seq refers to a not yet written record.
 */
bool prb_read_valid_info(struct printk_ringbuffer *rb, u64 seq,
			 struct printk_info *info, unsigned int *line_count)
{
	struct printk_record r;

	prb_rec_init_rd(&r, info, NULL, 0);

	return _prb_read_valid(rb, &seq, &r, line_count);
}

/**
 * prb_first_valid_seq() - Get the sequence number of the oldest available
 *                         record.
 *
 * @rb: The ringbuffer to get the sequence number from.
 *
 * This is the public function available to readers to see what the
 * first/oldest valid sequence number is.
 *
 * This provides readers a starting point to begin iterating the ringbuffer.
 *
 * Context: Any context.
 * Return: The sequence number of the first/oldest record or, if the
 *         ringbuffer is empty, 0 is returned.
 */
u64 prb_first_valid_seq(struct printk_ringbuffer *rb)
{
	u64 seq = 0;

	if (!_prb_read_valid(rb, &seq, NULL, NULL))
		return 0;

	return seq;
}

/**
 * prb_next_seq() - Get the sequence number after the last available record.
 *
 * @rb:  The ringbuffer to get the sequence number from.
 *
 * This is the public function available to readers to see what the next
 * newest sequence number available to readers will be.
 *
 * This provides readers a sequence number to jump to if all currently
 * available records should be skipped.
 *
 * Context: Any context.
 * Return: The sequence number of the next newest (not yet available) record
 *         for readers.
 */
u64 prb_next_seq(struct printk_ringbuffer *rb)
{
	u64 seq = 0;

	/* Search forward from the oldest descriptor. */
	while (_prb_read_valid(rb, &seq, NULL, NULL))
		seq++;

	return seq;
}

/**
 * prb_init() - Initialize a ringbuffer to use provided external buffers.
 *
 * @rb:       The ringbuffer to initialize.
 * @text_buf: The data buffer for text data.
 * @textbits: The size of @text_buf as a power-of-2 value.
 * @descs:    The descriptor buffer for ringbuffer records.
 * @descbits: The count of @descs items as a power-of-2 value.
 * @infos:    The printk_info buffer for ringbuffer records.
 *
 * This is the public function available to writers to setup a ringbuffer
 * during runtime using provided buffers.
 *
 * This must match the initialization of DEFINE_PRINTKRB().
 *
 * Context: Any context.
 */
void prb_init(struct printk_ringbuffer *rb,
	      char *text_buf, unsigned int textbits,
	      struct prb_desc *descs, unsigned int descbits,
	      struct printk_info *infos)
{
	memset(descs, 0, _DESCS_COUNT(descbits) * sizeof(descs[0]));
	memset(infos, 0, _DESCS_COUNT(descbits) * sizeof(infos[0]));

	rb->desc_ring.count_bits = descbits;
	rb->desc_ring.descs = descs;
	rb->desc_ring.infos = infos;
	atomic_long_set(&rb->desc_ring.head_id, DESC0_ID(descbits));
	atomic_long_set(&rb->desc_ring.tail_id, DESC0_ID(descbits));

	rb->text_data_ring.size_bits = textbits;
	rb->text_data_ring.data = text_buf;
	atomic_long_set(&rb->text_data_ring.head_lpos, BLK0_LPOS(textbits));
	atomic_long_set(&rb->text_data_ring.tail_lpos, BLK0_LPOS(textbits));

	atomic_long_set(&rb->fail, 0);

	atomic_long_set(&(descs[_DESCS_COUNT(descbits) - 1].state_var), DESC0_SV(descbits));
	descs[_DESCS_COUNT(descbits) - 1].text_blk_lpos.begin = FAILED_LPOS;
	descs[_DESCS_COUNT(descbits) - 1].text_blk_lpos.next = FAILED_LPOS;

	infos[0].seq = -(u64)_DESCS_COUNT(descbits);
	infos[_DESCS_COUNT(descbits) - 1].seq = 0;
}

/**
 * prb_record_text_space() - Query the full actual used ringbuffer space for
 *                           the text data of a reserved entry.
 *
 * @e: The successfully reserved entry to query.
 *
 * This is the public function available to writers to see how much actual
 * space is used in the ringbuffer to store the text data of the specified
 * entry.
 *
 * This function is only valid if @e has been successfully reserved using
 * prb_reserve().
 *
 * Context: Any context.
 * Return: The size in bytes used by the text data of the associated record.
 */
unsigned int prb_record_text_space(struct prb_reserved_entry *e)
{
	return e->text_space;
}
