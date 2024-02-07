/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _KERNEL_PRINTK_RINGBUFFER_H
#define _KERNEL_PRINTK_RINGBUFFER_H

#include <linux/atomic.h>
#include <linux/dev_printk.h>

/*
 * Meta information about each stored message.
 *
 * All fields are set by the printk code except for @seq, which is
 * set by the ringbuffer code.
 */
struct printk_info {
	u64	seq;		/* sequence number */
	u64	ts_nsec;	/* timestamp in nanoseconds */
	u16	text_len;	/* length of text message */
	u8	facility;	/* syslog facility */
	u8	flags:5;	/* internal record flags */
	u8	level:3;	/* syslog level */
	u32	caller_id;	/* thread id or processor id */

	struct dev_printk_info	dev_info;
};

/*
 * A structure providing the buffers, used by writers and readers.
 *
 * Writers:
 * Using prb_rec_init_wr(), a writer sets @text_buf_size before calling
 * prb_reserve(). On success, prb_reserve() sets @info and @text_buf to
 * buffers reserved for that writer.
 *
 * Readers:
 * Using prb_rec_init_rd(), a reader sets all fields before calling
 * prb_read_valid(). Note that the reader provides the @info and @text_buf,
 * buffers. On success, the struct pointed to by @info will be filled and
 * the char array pointed to by @text_buf will be filled with text data.
 */
struct printk_record {
	struct printk_info	*info;
	char			*text_buf;
	unsigned int		text_buf_size;
};

/* Specifies the logical position and span of a data block. */
struct prb_data_blk_lpos {
	unsigned long	begin;
	unsigned long	next;
};

/*
 * A descriptor: the complete meta-data for a record.
 *
 * @state_var: A bitwise combination of descriptor ID and descriptor state.
 */
struct prb_desc {
	atomic_long_t			state_var;
	struct prb_data_blk_lpos	text_blk_lpos;
};

/* A ringbuffer of "ID + data" elements. */
struct prb_data_ring {
	unsigned int	size_bits;
	char		*data;
	atomic_long_t	head_lpos;
	atomic_long_t	tail_lpos;
};

/* A ringbuffer of "struct prb_desc" elements. */
struct prb_desc_ring {
	unsigned int		count_bits;
	struct prb_desc		*descs;
	struct printk_info	*infos;
	atomic_long_t		head_id;
	atomic_long_t		tail_id;
	atomic_long_t		last_finalized_id;
};

/*
 * The high level structure representing the printk ringbuffer.
 *
 * @fail: Count of failed prb_reserve() calls where not even a data-less
 *        record was created.
 */
struct printk_ringbuffer {
	struct prb_desc_ring	desc_ring;
	struct prb_data_ring	text_data_ring;
	atomic_long_t		fail;
};

/*
 * Used by writers as a reserve/commit handle.
 *
 * @rb:         Ringbuffer where the entry is reserved.
 * @irqflags:   Saved irq flags to restore on entry commit.
 * @id:         ID of the reserved descriptor.
 * @text_space: Total occupied buffer space in the text data ring, including
 *              ID, alignment padding, and wrapping data blocks.
 *
 * This structure is an opaque handle for writers. Its contents are only
 * to be used by the ringbuffer implementation.
 */
struct prb_reserved_entry {
	struct printk_ringbuffer	*rb;
	unsigned long			irqflags;
	unsigned long			id;
	unsigned int			text_space;
};

/* The possible responses of a descriptor state-query. */
enum desc_state {
	desc_miss	=  -1,	/* ID mismatch (pseudo state) */
	desc_reserved	= 0x0,	/* reserved, in use by writer */
	desc_committed	= 0x1,	/* committed by writer, could get reopened */
	desc_finalized	= 0x2,	/* committed, no further modification allowed */
	desc_reusable	= 0x3,	/* free, not yet used by any writer */
};

#define _DATA_SIZE(sz_bits)	(1UL << (sz_bits))
#define _DESCS_COUNT(ct_bits)	(1U << (ct_bits))
#define DESC_SV_BITS		(sizeof(unsigned long) * 8)
#define DESC_FLAGS_SHIFT	(DESC_SV_BITS - 2)
#define DESC_FLAGS_MASK		(3UL << DESC_FLAGS_SHIFT)
#define DESC_STATE(sv)		(3UL & (sv >> DESC_FLAGS_SHIFT))
#define DESC_SV(id, state)	(((unsigned long)state << DESC_FLAGS_SHIFT) | id)
#define DESC_ID_MASK		(~DESC_FLAGS_MASK)
#define DESC_ID(sv)		((sv) & DESC_ID_MASK)
#define FAILED_LPOS		0x1
#define NO_LPOS			0x3

#define FAILED_BLK_LPOS	\
{				\
	.begin	= FAILED_LPOS,	\
	.next	= FAILED_LPOS,	\
}

/*
 * Descriptor Bootstrap
 *
 * The descriptor array is minimally initialized to allow immediate usage
 * by readers and writers. The requirements that the descriptor array
 * initialization must satisfy:
 *
 *   Req1
 *     The tail must point to an existing (committed or reusable) descriptor.
 *     This is required by the implementation of prb_first_seq().
 *
 *   Req2
 *     Readers must see that the ringbuffer is initially empty.
 *
 *   Req3
 *     The first record reserved by a writer is assigned sequence number 0.
 *
 * To satisfy Req1, the tail initially points to a descriptor that is
 * minimally initialized (having no data block, i.e. data-less with the
 * data block's lpos @begin and @next values set to FAILED_LPOS).
 *
 * To satisfy Req2, the initial tail descriptor is initialized to the
 * reusable state. Readers recognize reusable descriptors as existing
 * records, but skip over them.
 *
 * To satisfy Req3, the last descriptor in the array is used as the initial
 * head (and tail) descriptor. This allows the first record reserved by a
 * writer (head + 1) to be the first descriptor in the array. (Only the first
 * descriptor in the array could have a valid sequence number of 0.)
 *
 * The first time a descriptor is reserved, it is assigned a sequence number
 * with the value of the array index. A "first time reserved" descriptor can
 * be recognized because it has a sequence number of 0 but does not have an
 * index of 0. (Only the first descriptor in the array could have a valid
 * sequence number of 0.) After the first reservation, all future reservations
 * (recycling) simply involve incrementing the sequence number by the array
 * count.
 *
 *   Hack #1
 *     Only the first descriptor in the array is allowed to have the sequence
 *     number 0. In this case it is not possible to recognize if it is being
 *     reserved the first time (set to index value) or has been reserved
 *     previously (increment by the array count). This is handled by _always_
 *     incrementing the sequence number by the array count when reserving the
 *     first descriptor in the array. In order to satisfy Req3, the sequence
 *     number of the first descriptor in the array is initialized to minus
 *     the array count. Then, upon the first reservation, it is incremented
 *     to 0, thus satisfying Req3.
 *
 *   Hack #2
 *     prb_first_seq() can be called at any time by readers to retrieve the
 *     sequence number of the tail descriptor. However, due to Req2 and Req3,
 *     initially there are no records to report the sequence number of
 *     (sequence numbers are u64 and there is nothing less than 0). To handle
 *     this, the sequence number of the initial tail descriptor is initialized
 *     to 0. Technically this is incorrect, because there is no record with
 *     sequence number 0 (yet) and the tail descriptor is not the first
 *     descriptor in the array. But it allows prb_read_valid() to correctly
 *     report the existence of a record for _any_ given sequence number at all
 *     times. Bootstrapping is complete when the tail is pushed the first
 *     time, thus finally pointing to the first descriptor reserved by a
 *     writer, which has the assigned sequence number 0.
 */

/*
 * Initiating Logical Value Overflows
 *
 * Both logical position (lpos) and ID values can be mapped to array indexes
 * but may experience overflows during the lifetime of the system. To ensure
 * that printk_ringbuffer can handle the overflows for these types, initial
 * values are chosen that map to the correct initial array indexes, but will
 * result in overflows soon.
 *
 *   BLK0_LPOS
 *     The initial @head_lpos and @tail_lpos for data rings. It is at index
 *     0 and the lpos value is such that it will overflow on the first wrap.
 *
 *   DESC0_ID
 *     The initial @head_id and @tail_id for the desc ring. It is at the last
 *     index of the descriptor array (see Req3 above) and the ID value is such
 *     that it will overflow on the second wrap.
 */
#define BLK0_LPOS(sz_bits)	(-(_DATA_SIZE(sz_bits)))
#define DESC0_ID(ct_bits)	DESC_ID(-(_DESCS_COUNT(ct_bits) + 1))
#define DESC0_SV(ct_bits)	DESC_SV(DESC0_ID(ct_bits), desc_reusable)

/*
 * Define a ringbuffer with an external text data buffer. The same as
 * DEFINE_PRINTKRB() but requires specifying an external buffer for the
 * text data.
 *
 * Note: The specified external buffer must be of the size:
 *       2 ^ (descbits + avgtextbits)
 */
#define _DEFINE_PRINTKRB(name, descbits, avgtextbits, text_buf)			\
static struct prb_desc _##name##_descs[_DESCS_COUNT(descbits)] = {				\
	/* the initial head and tail */								\
	[_DESCS_COUNT(descbits) - 1] = {							\
		/* reusable */									\
		.state_var	= ATOMIC_INIT(DESC0_SV(descbits)),				\
		/* no associated data block */							\
		.text_blk_lpos	= FAILED_BLK_LPOS,						\
	},											\
};												\
static struct printk_info _##name##_infos[_DESCS_COUNT(descbits)] = {				\
	/* this will be the first record reserved by a writer */				\
	[0] = {											\
		/* will be incremented to 0 on the first reservation */				\
		.seq = -(u64)_DESCS_COUNT(descbits),						\
	},											\
	/* the initial head and tail */								\
	[_DESCS_COUNT(descbits) - 1] = {							\
		/* reports the first seq value during the bootstrap phase */			\
		.seq = 0,									\
	},											\
};												\
static struct printk_ringbuffer name = {							\
	.desc_ring = {										\
		.count_bits	= descbits,							\
		.descs		= &_##name##_descs[0],						\
		.infos		= &_##name##_infos[0],						\
		.head_id	= ATOMIC_INIT(DESC0_ID(descbits)),				\
		.tail_id	= ATOMIC_INIT(DESC0_ID(descbits)),				\
		.last_finalized_id = ATOMIC_INIT(DESC0_ID(descbits)),				\
	},											\
	.text_data_ring = {									\
		.size_bits	= (avgtextbits) + (descbits),					\
		.data		= text_buf,							\
		.head_lpos	= ATOMIC_LONG_INIT(BLK0_LPOS((avgtextbits) + (descbits))),	\
		.tail_lpos	= ATOMIC_LONG_INIT(BLK0_LPOS((avgtextbits) + (descbits))),	\
	},											\
	.fail			= ATOMIC_LONG_INIT(0),						\
}

/**
 * DEFINE_PRINTKRB() - Define a ringbuffer.
 *
 * @name:        The name of the ringbuffer variable.
 * @descbits:    The number of descriptors as a power-of-2 value.
 * @avgtextbits: The average text data size per record as a power-of-2 value.
 *
 * This is a macro for defining a ringbuffer and all internal structures
 * such that it is ready for immediate use. See _DEFINE_PRINTKRB() for a
 * variant where the text data buffer can be specified externally.
 */
#define DEFINE_PRINTKRB(name, descbits, avgtextbits)				\
static char _##name##_text[1U << ((avgtextbits) + (descbits))]			\
			__aligned(__alignof__(unsigned long));			\
_DEFINE_PRINTKRB(name, descbits, avgtextbits, &_##name##_text[0])

/* Writer Interface */

/**
 * prb_rec_init_wr() - Initialize a buffer for writing records.
 *
 * @r:             The record to initialize.
 * @text_buf_size: The needed text buffer size.
 */
static inline void prb_rec_init_wr(struct printk_record *r,
				   unsigned int text_buf_size)
{
	r->info = NULL;
	r->text_buf = NULL;
	r->text_buf_size = text_buf_size;
}

bool prb_reserve(struct prb_reserved_entry *e, struct printk_ringbuffer *rb,
		 struct printk_record *r);
bool prb_reserve_in_last(struct prb_reserved_entry *e, struct printk_ringbuffer *rb,
			 struct printk_record *r, u32 caller_id, unsigned int max_size);
void prb_commit(struct prb_reserved_entry *e);
void prb_final_commit(struct prb_reserved_entry *e);

void prb_init(struct printk_ringbuffer *rb,
	      char *text_buf, unsigned int text_buf_size,
	      struct prb_desc *descs, unsigned int descs_count_bits,
	      struct printk_info *infos);
unsigned int prb_record_text_space(struct prb_reserved_entry *e);

/* Reader Interface */

/**
 * prb_rec_init_rd() - Initialize a buffer for reading records.
 *
 * @r:             The record to initialize.
 * @info:          A buffer to store record meta-data.
 * @text_buf:      A buffer to store text data.
 * @text_buf_size: The size of @text_buf.
 *
 * Initialize all the fields that a reader is interested in. All arguments
 * (except @r) are optional. Only record data for arguments that are
 * non-NULL or non-zero will be read.
 */
static inline void prb_rec_init_rd(struct printk_record *r,
				   struct printk_info *info,
				   char *text_buf, unsigned int text_buf_size)
{
	r->info = info;
	r->text_buf = text_buf;
	r->text_buf_size = text_buf_size;
}

/**
 * prb_for_each_record() - Iterate over the records of a ringbuffer.
 *
 * @from: The sequence number to begin with.
 * @rb:   The ringbuffer to iterate over.
 * @s:    A u64 to store the sequence number on each iteration.
 * @r:    A printk_record to store the record on each iteration.
 *
 * This is a macro for conveniently iterating over a ringbuffer.
 * Note that @s may not be the sequence number of the record on each
 * iteration. For the sequence number, @r->info->seq should be checked.
 *
 * Context: Any context.
 */
#define prb_for_each_record(from, rb, s, r) \
for ((s) = from; prb_read_valid(rb, s, r); (s) = (r)->info->seq + 1)

/**
 * prb_for_each_info() - Iterate over the meta data of a ringbuffer.
 *
 * @from: The sequence number to begin with.
 * @rb:   The ringbuffer to iterate over.
 * @s:    A u64 to store the sequence number on each iteration.
 * @i:    A printk_info to store the record meta data on each iteration.
 * @lc:   An unsigned int to store the text line count of each record.
 *
 * This is a macro for conveniently iterating over a ringbuffer.
 * Note that @s may not be the sequence number of the record on each
 * iteration. For the sequence number, @r->info->seq should be checked.
 *
 * Context: Any context.
 */
#define prb_for_each_info(from, rb, s, i, lc) \
for ((s) = from; prb_read_valid_info(rb, s, i, lc); (s) = (i)->seq + 1)

bool prb_read_valid(struct printk_ringbuffer *rb, u64 seq,
		    struct printk_record *r);
bool prb_read_valid_info(struct printk_ringbuffer *rb, u64 seq,
			 struct printk_info *info, unsigned int *line_count);

u64 prb_first_seq(struct printk_ringbuffer *rb);
u64 prb_first_valid_seq(struct printk_ringbuffer *rb);
u64 prb_next_seq(struct printk_ringbuffer *rb);

#ifdef CONFIG_64BIT

#define __u64seq_to_ulseq(u64seq) (u64seq)
#define __ulseq_to_u64seq(rb, ulseq) (ulseq)

#else /* CONFIG_64BIT */

#define __u64seq_to_ulseq(u64seq) ((u32)u64seq)

static inline u64 __ulseq_to_u64seq(struct printk_ringbuffer *rb, u32 ulseq)
{
	u64 rb_first_seq = prb_first_seq(rb);
	u64 seq;

	/*
	 * The provided sequence is only the lower 32 bits of the ringbuffer
	 * sequence. It needs to be expanded to 64bit. Get the first sequence
	 * number from the ringbuffer and fold it.
	 *
	 * Having a 32bit representation in the console is sufficient.
	 * If a console ever gets more than 2^31 records behind
	 * the ringbuffer then this is the least of the problems.
	 *
	 * Also the access to the ring buffer is always safe.
	 */
	seq = rb_first_seq - (s32)((u32)rb_first_seq - ulseq);

	return seq;
}

#endif /* CONFIG_64BIT */

#endif /* _KERNEL_PRINTK_RINGBUFFER_H */
