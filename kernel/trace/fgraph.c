// SPDX-License-Identifier: GPL-2.0
/*
 * Infrastructure to took into function calls and returns.
 * Copyright (c) 2008-2009 Frederic Weisbecker <fweisbec@gmail.com>
 * Mostly borrowed from function tracer which
 * is Copyright (c) Steven Rostedt <srostedt@redhat.com>
 *
 * Highly modified by Steven Rostedt (VMware).
 */
#include <linux/bits.h>
#include <linux/jump_label.h>
#include <linux/suspend.h>
#include <linux/ftrace.h>
#include <linux/static_call.h>
#include <linux/slab.h>

#include <trace/events/sched.h>

#include "ftrace_internal.h"
#include "trace.h"

/*
 * FGRAPH_FRAME_SIZE:	Size in bytes of the meta data on the shadow stack
 * FGRAPH_FRAME_OFFSET:	Size in long words of the meta data frame
 */
#define FGRAPH_FRAME_SIZE	sizeof(struct ftrace_ret_stack)
#define FGRAPH_FRAME_OFFSET	DIV_ROUND_UP(FGRAPH_FRAME_SIZE, sizeof(long))

/*
 * On entry to a function (via function_graph_enter()), a new fgraph frame
 * (ftrace_ret_stack) is pushed onto the stack as well as a word that
 * holds a bitmask and a type (called "bitmap"). The bitmap is defined as:
 *
 * bits:  0 -  9	offset in words from the previous ftrace_ret_stack
 *
 * bits: 10 - 11	Type of storage
 *			  0 - reserved
 *			  1 - bitmap of fgraph_array index
 *			  2 - reserved data
 *
 * For type with "bitmap of fgraph_array index" (FGRAPH_TYPE_BITMAP):
 *  bits: 12 - 27	The bitmap of fgraph_ops fgraph_array index
 *			That is, it's a bitmask of 0-15 (16 bits)
 *			where if a corresponding ops in the fgraph_array[]
 *			expects a callback from the return of the function
 *			it's corresponding bit will be set.
 *
 *
 * The top of the ret_stack (when not empty) will always have a reference
 * word that points to the last fgraph frame that was saved.
 *
 * For reserved data:
 *  bits: 12 - 17	The size in words that is stored
 *  bits: 18 - 23	The index of fgraph_array, which shows who is stored
 *
 * That is, at the end of function_graph_enter, if the first and forth
 * fgraph_ops on the fgraph_array[] (index 0 and 3) needs their retfunc called
 * on the return of the function being traced, and the forth fgraph_ops
 * stored two words of data, this is what will be on the task's shadow
 * ret_stack: (the stack grows upward)
 *
 *  ret_stack[SHADOW_STACK_OFFSET]
 * | SHADOW_STACK_TASK_VARS(ret_stack)[15]      |
 * ...
 * | SHADOW_STACK_TASK_VARS(ret_stack)[0]       |
 *  ret_stack[SHADOW_STACK_MAX_OFFSET]
 * ...
 * |                                            | <- task->curr_ret_stack
 * +--------------------------------------------+
 * | (3 << 12) | (3 << 10) | FGRAPH_FRAME_OFFSET|
 * |         *or put another way*               |
 * | (3 << FGRAPH_DATA_INDEX_SHIFT)| \          | This is for fgraph_ops[3].
 * | ((2 - 1) << FGRAPH_DATA_SHIFT)| \          | The data size is 2 words.
 * | (FGRAPH_TYPE_DATA << FGRAPH_TYPE_SHIFT)| \ |
 * | (offset2:FGRAPH_FRAME_OFFSET+3)            | <- the offset2 is from here
 * +--------------------------------------------+ ( It is 4 words from the ret_stack)
 * |            STORED DATA WORD 2              |
 * |            STORED DATA WORD 1              |
 * +--------------------------------------------+
 * | (9 << 12) | (1 << 10) | FGRAPH_FRAME_OFFSET|
 * |         *or put another way*               |
 * | (BIT(3)|BIT(0)) << FGRAPH_INDEX_SHIFT | \  |
 * | FGRAPH_TYPE_BITMAP << FGRAPH_TYPE_SHIFT| \ |
 * | (offset1:FGRAPH_FRAME_OFFSET)              | <- the offset1 is from here
 * +--------------------------------------------+
 * | struct ftrace_ret_stack                    |
 * |   (stores the saved ret pointer)           | <- the offset points here
 * +--------------------------------------------+
 * |                 (X) | (N)                  | ( N words away from
 * |                                            |   previous ret_stack)
 * ...
 * ret_stack[0]
 *
 * If a backtrace is required, and the real return pointer needs to be
 * fetched, then it looks at the task's curr_ret_stack offset, if it
 * is greater than zero (reserved, or right before popped), it would mask
 * the value by FGRAPH_FRAME_OFFSET_MASK to get the offset of the
 * ftrace_ret_stack structure stored on the shadow stack.
 */

/*
 * The following is for the top word on the stack:
 *
 *   FGRAPH_FRAME_OFFSET (0-9) holds the offset delta to the fgraph frame
 *   FGRAPH_TYPE (10-11) holds the type of word this is.
 *     (RESERVED or BITMAP)
 */
#define FGRAPH_FRAME_OFFSET_BITS	10
#define FGRAPH_FRAME_OFFSET_MASK	GENMASK(FGRAPH_FRAME_OFFSET_BITS - 1, 0)

#define FGRAPH_TYPE_BITS	2
#define FGRAPH_TYPE_MASK	GENMASK(FGRAPH_TYPE_BITS - 1, 0)
#define FGRAPH_TYPE_SHIFT	FGRAPH_FRAME_OFFSET_BITS

enum {
	FGRAPH_TYPE_RESERVED	= 0,
	FGRAPH_TYPE_BITMAP	= 1,
	FGRAPH_TYPE_DATA	= 2,
};

/*
 * For BITMAP type:
 *   FGRAPH_INDEX (12-27) bits holding the gops index wanting return callback called
 */
#define FGRAPH_INDEX_BITS	16
#define FGRAPH_INDEX_MASK	GENMASK(FGRAPH_INDEX_BITS - 1, 0)
#define FGRAPH_INDEX_SHIFT	(FGRAPH_TYPE_SHIFT + FGRAPH_TYPE_BITS)

/*
 * For DATA type:
 *  FGRAPH_DATA (12-17) bits hold the size of data (in words)
 *  FGRAPH_INDEX (18-23) bits hold the index for which gops->idx the data is for
 *
 * Note:
 *  data_size == 0 means 1 word, and 31 (=2^5 - 1) means 32 words.
 */
#define FGRAPH_DATA_BITS	5
#define FGRAPH_DATA_MASK	GENMASK(FGRAPH_DATA_BITS - 1, 0)
#define FGRAPH_DATA_SHIFT	(FGRAPH_TYPE_SHIFT + FGRAPH_TYPE_BITS)
#define FGRAPH_MAX_DATA_SIZE (sizeof(long) * (1 << FGRAPH_DATA_BITS))

#define FGRAPH_DATA_INDEX_BITS	4
#define FGRAPH_DATA_INDEX_MASK	GENMASK(FGRAPH_DATA_INDEX_BITS - 1, 0)
#define FGRAPH_DATA_INDEX_SHIFT	(FGRAPH_DATA_SHIFT + FGRAPH_DATA_BITS)

#define FGRAPH_MAX_INDEX	\
	((FGRAPH_INDEX_SIZE << FGRAPH_DATA_BITS) + FGRAPH_RET_INDEX)

#define FGRAPH_ARRAY_SIZE	FGRAPH_INDEX_BITS

/*
 * SHADOW_STACK_SIZE:	The size in bytes of the entire shadow stack
 * SHADOW_STACK_OFFSET:	The size in long words of the shadow stack
 * SHADOW_STACK_MAX_OFFSET: The max offset of the stack for a new frame to be added
 */
#define SHADOW_STACK_SIZE	(4096)
#define SHADOW_STACK_OFFSET	(SHADOW_STACK_SIZE / sizeof(long))
/* Leave on a buffer at the end */
#define SHADOW_STACK_MAX_OFFSET				\
	(SHADOW_STACK_OFFSET - (FGRAPH_FRAME_OFFSET + 1 + FGRAPH_ARRAY_SIZE))

/* RET_STACK():		Return the frame from a given @offset from task @t */
#define RET_STACK(t, offset) ((struct ftrace_ret_stack *)(&(t)->ret_stack[offset]))

/*
 * Each fgraph_ops has a reservered unsigned long at the end (top) of the
 * ret_stack to store task specific state.
 */
#define SHADOW_STACK_TASK_VARS(ret_stack) \
	((unsigned long *)(&(ret_stack)[SHADOW_STACK_OFFSET - FGRAPH_ARRAY_SIZE]))

DEFINE_STATIC_KEY_FALSE(kill_ftrace_graph);
int ftrace_graph_active;

static struct kmem_cache *fgraph_stack_cachep;

static struct fgraph_ops *fgraph_array[FGRAPH_ARRAY_SIZE];
static unsigned long fgraph_array_bitmask;

/* LRU index table for fgraph_array */
static int fgraph_lru_table[FGRAPH_ARRAY_SIZE];
static int fgraph_lru_next;
static int fgraph_lru_last;

/* Initialize fgraph_lru_table with unused index */
static void fgraph_lru_init(void)
{
	int i;

	for (i = 0; i < FGRAPH_ARRAY_SIZE; i++)
		fgraph_lru_table[i] = i;
}

/* Release the used index to the LRU table */
static int fgraph_lru_release_index(int idx)
{
	if (idx < 0 || idx >= FGRAPH_ARRAY_SIZE ||
	    WARN_ON_ONCE(fgraph_lru_table[fgraph_lru_last] != -1))
		return -1;

	fgraph_lru_table[fgraph_lru_last] = idx;
	fgraph_lru_last = (fgraph_lru_last + 1) % FGRAPH_ARRAY_SIZE;

	clear_bit(idx, &fgraph_array_bitmask);
	return 0;
}

/* Allocate a new index from LRU table */
static int fgraph_lru_alloc_index(void)
{
	int idx = fgraph_lru_table[fgraph_lru_next];

	/* No id is available */
	if (idx == -1)
		return -1;

	fgraph_lru_table[fgraph_lru_next] = -1;
	fgraph_lru_next = (fgraph_lru_next + 1) % FGRAPH_ARRAY_SIZE;

	set_bit(idx, &fgraph_array_bitmask);
	return idx;
}

/* Get the offset to the fgraph frame from a ret_stack value */
static inline int __get_offset(unsigned long val)
{
	return val & FGRAPH_FRAME_OFFSET_MASK;
}

/* Get the type of word from a ret_stack value */
static inline int __get_type(unsigned long val)
{
	return (val >> FGRAPH_TYPE_SHIFT) & FGRAPH_TYPE_MASK;
}

/* Get the data_index for a DATA type ret_stack word */
static inline int __get_data_index(unsigned long val)
{
	return (val >> FGRAPH_DATA_INDEX_SHIFT) & FGRAPH_DATA_INDEX_MASK;
}

/* Get the data_size for a DATA type ret_stack word */
static inline int __get_data_size(unsigned long val)
{
	return ((val >> FGRAPH_DATA_SHIFT) & FGRAPH_DATA_MASK) + 1;
}

/* Get the word from the ret_stack at @offset */
static inline unsigned long get_fgraph_entry(struct task_struct *t, int offset)
{
	return t->ret_stack[offset];
}

/* Get the FRAME_OFFSET from the word from the @offset on ret_stack */
static inline int get_frame_offset(struct task_struct *t, int offset)
{
	return __get_offset(t->ret_stack[offset]);
}

/* For BITMAP type: get the bitmask from the @offset at ret_stack */
static inline unsigned long
get_bitmap_bits(struct task_struct *t, int offset)
{
	return (t->ret_stack[offset] >> FGRAPH_INDEX_SHIFT) & FGRAPH_INDEX_MASK;
}

/* Write the bitmap to the ret_stack at @offset (does index, offset and bitmask) */
static inline void
set_bitmap(struct task_struct *t, int offset, unsigned long bitmap)
{
	t->ret_stack[offset] = (bitmap << FGRAPH_INDEX_SHIFT) |
		(FGRAPH_TYPE_BITMAP << FGRAPH_TYPE_SHIFT) | FGRAPH_FRAME_OFFSET;
}

/* For DATA type: get the data saved under the ret_stack word at @offset */
static inline void *get_data_type_data(struct task_struct *t, int offset)
{
	unsigned long val = t->ret_stack[offset];

	if (__get_type(val) != FGRAPH_TYPE_DATA)
		return NULL;
	offset -= __get_data_size(val);
	return (void *)&t->ret_stack[offset];
}

/* Create the ret_stack word for a DATA type */
static inline unsigned long make_data_type_val(int idx, int size, int offset)
{
	return (idx << FGRAPH_DATA_INDEX_SHIFT) |
		((size - 1) << FGRAPH_DATA_SHIFT) |
		(FGRAPH_TYPE_DATA << FGRAPH_TYPE_SHIFT) | offset;
}

/* ftrace_graph_entry set to this to tell some archs to run function graph */
static int entry_run(struct ftrace_graph_ent *trace, struct fgraph_ops *ops)
{
	return 0;
}

/* ftrace_graph_return set to this to tell some archs to run function graph */
static void return_run(struct ftrace_graph_ret *trace, struct fgraph_ops *ops)
{
}

static void ret_stack_set_task_var(struct task_struct *t, int idx, long val)
{
	unsigned long *gvals = SHADOW_STACK_TASK_VARS(t->ret_stack);

	gvals[idx] = val;
}

static unsigned long *
ret_stack_get_task_var(struct task_struct *t, int idx)
{
	unsigned long *gvals = SHADOW_STACK_TASK_VARS(t->ret_stack);

	return &gvals[idx];
}

static void ret_stack_init_task_vars(unsigned long *ret_stack)
{
	unsigned long *gvals = SHADOW_STACK_TASK_VARS(ret_stack);

	memset(gvals, 0, sizeof(*gvals) * FGRAPH_ARRAY_SIZE);
}

/**
 * fgraph_reserve_data - Reserve storage on the task's ret_stack
 * @idx:	The index of fgraph_array
 * @size_bytes: The size in bytes to reserve
 *
 * Reserves space of up to FGRAPH_MAX_DATA_SIZE bytes on the
 * task's ret_stack shadow stack, for a given fgraph_ops during
 * the entryfunc() call. If entryfunc() returns zero, the storage
 * is discarded. An entryfunc() can only call this once per iteration.
 * The fgraph_ops retfunc() can retrieve this stored data with
 * fgraph_retrieve_data().
 *
 * Returns: On success, a pointer to the data on the stack.
 *   Otherwise, NULL if there's not enough space left on the
 *   ret_stack for the data, or if fgraph_reserve_data() was called
 *   more than once for a single entryfunc() call.
 */
void *fgraph_reserve_data(int idx, int size_bytes)
{
	unsigned long val;
	void *data;
	int curr_ret_stack = current->curr_ret_stack;
	int data_size;

	if (size_bytes > FGRAPH_MAX_DATA_SIZE)
		return NULL;

	/* Convert the data size to number of longs. */
	data_size = (size_bytes + sizeof(long) - 1) >> (sizeof(long) == 4 ? 2 : 3);

	val = get_fgraph_entry(current, curr_ret_stack - 1);
	data = &current->ret_stack[curr_ret_stack];

	curr_ret_stack += data_size + 1;
	if (unlikely(curr_ret_stack >= SHADOW_STACK_MAX_OFFSET))
		return NULL;

	val = make_data_type_val(idx, data_size, __get_offset(val) + data_size + 1);

	/* Set the last word to be reserved */
	current->ret_stack[curr_ret_stack - 1] = val;

	/* Make sure interrupts see this */
	barrier();
	current->curr_ret_stack = curr_ret_stack;
	/* Again sync with interrupts, and reset reserve */
	current->ret_stack[curr_ret_stack - 1] = val;

	return data;
}

/**
 * fgraph_retrieve_data - Retrieve stored data from fgraph_reserve_data()
 * @idx:	the index of fgraph_array (fgraph_ops::idx)
 * @size_bytes: pointer to retrieved data size.
 *
 * This is to be called by a fgraph_ops retfunc(), to retrieve data that
 * was stored by the fgraph_ops entryfunc() on the function entry.
 * That is, this will retrieve the data that was reserved on the
 * entry of the function that corresponds to the exit of the function
 * that the fgraph_ops retfunc() is called on.
 *
 * Returns: The stored data from fgraph_reserve_data() called by the
 *    matching entryfunc() for the retfunc() this is called from.
 *   Or NULL if there was nothing stored.
 */
void *fgraph_retrieve_data(int idx, int *size_bytes)
{
	return fgraph_retrieve_parent_data(idx, size_bytes, 0);
}

/**
 * fgraph_get_task_var - retrieve a task specific state variable
 * @gops: The ftrace_ops that owns the task specific variable
 *
 * Every registered fgraph_ops has a task state variable
 * reserved on the task's ret_stack. This function returns the
 * address to that variable.
 *
 * Returns the address to the fgraph_ops @gops tasks specific
 * unsigned long variable.
 */
unsigned long *fgraph_get_task_var(struct fgraph_ops *gops)
{
	return ret_stack_get_task_var(current, gops->idx);
}

/*
 * @offset: The offset into @t->ret_stack to find the ret_stack entry
 * @frame_offset: Where to place the offset into @t->ret_stack of that entry
 *
 * Returns a pointer to the previous ret_stack below @offset or NULL
 *   when it reaches the bottom of the stack.
 *
 * Calling this with:
 *
 *   offset = task->curr_ret_stack;
 *   do {
 *	ret_stack = get_ret_stack(task, offset, &offset);
 *   } while (ret_stack);
 *
 * Will iterate through all the ret_stack entries from curr_ret_stack
 * down to the first one.
 */
static inline struct ftrace_ret_stack *
get_ret_stack(struct task_struct *t, int offset, int *frame_offset)
{
	int offs;

	BUILD_BUG_ON(FGRAPH_FRAME_SIZE % sizeof(long));

	if (unlikely(offset <= 0))
		return NULL;

	offs = get_frame_offset(t, --offset);
	if (WARN_ON_ONCE(offs <= 0 || offs > offset))
		return NULL;

	offset -= offs;

	*frame_offset = offset;
	return RET_STACK(t, offset);
}

/**
 * fgraph_retrieve_parent_data - get data from a parent function
 * @idx: The index into the fgraph_array (fgraph_ops::idx)
 * @size_bytes: A pointer to retrieved data size
 * @depth: The depth to find the parent (0 is the current function)
 *
 * This is similar to fgraph_retrieve_data() but can be used to retrieve
 * data from a parent caller function.
 *
 * Return: a pointer to the specified parent data or NULL if not found
 */
void *fgraph_retrieve_parent_data(int idx, int *size_bytes, int depth)
{
	struct ftrace_ret_stack *ret_stack = NULL;
	int offset = current->curr_ret_stack;
	unsigned long val;

	if (offset <= 0)
		return NULL;

	for (;;) {
		int next_offset;

		ret_stack = get_ret_stack(current, offset, &next_offset);
		if (!ret_stack || --depth < 0)
			break;
		offset = next_offset;
	}

	if (!ret_stack)
		return NULL;

	offset--;

	val = get_fgraph_entry(current, offset);
	while (__get_type(val) == FGRAPH_TYPE_DATA) {
		if (__get_data_index(val) == idx)
			goto found;
		offset -= __get_data_size(val) + 1;
		val = get_fgraph_entry(current, offset);
	}
	return NULL;
found:
	if (size_bytes)
		*size_bytes = __get_data_size(val) * sizeof(long);
	return get_data_type_data(current, offset);
}

/* Both enabled by default (can be cleared by function_graph tracer flags */
bool fgraph_sleep_time = true;

#ifdef CONFIG_DYNAMIC_FTRACE
/*
 * archs can override this function if they must do something
 * to enable hook for graph tracer.
 */
int __weak ftrace_enable_ftrace_graph_caller(void)
{
	return 0;
}

/*
 * archs can override this function if they must do something
 * to disable hook for graph tracer.
 */
int __weak ftrace_disable_ftrace_graph_caller(void)
{
	return 0;
}
#endif

int ftrace_graph_entry_stub(struct ftrace_graph_ent *trace,
			    struct fgraph_ops *gops)
{
	return 0;
}

static void ftrace_graph_ret_stub(struct ftrace_graph_ret *trace,
				  struct fgraph_ops *gops)
{
}

static struct fgraph_ops fgraph_stub = {
	.entryfunc = ftrace_graph_entry_stub,
	.retfunc = ftrace_graph_ret_stub,
};

static struct fgraph_ops *fgraph_direct_gops = &fgraph_stub;
DEFINE_STATIC_CALL(fgraph_func, ftrace_graph_entry_stub);
DEFINE_STATIC_CALL(fgraph_retfunc, ftrace_graph_ret_stub);
static DEFINE_STATIC_KEY_TRUE(fgraph_do_direct);

/**
 * ftrace_graph_stop - set to permanently disable function graph tracing
 *
 * In case of an error int function graph tracing, this is called
 * to try to keep function graph tracing from causing any more harm.
 * Usually this is pretty severe and this is called to try to at least
 * get a warning out to the user.
 */
void ftrace_graph_stop(void)
{
	static_branch_enable(&kill_ftrace_graph);
}

/* Add a function return address to the trace stack on thread info.*/
static int
ftrace_push_return_trace(unsigned long ret, unsigned long func,
			 unsigned long frame_pointer, unsigned long *retp,
			 int fgraph_idx)
{
	struct ftrace_ret_stack *ret_stack;
	unsigned long val;
	int offset;

	if (unlikely(ftrace_graph_is_dead()))
		return -EBUSY;

	if (!current->ret_stack)
		return -EBUSY;

	BUILD_BUG_ON(SHADOW_STACK_SIZE % sizeof(long));

	/* Set val to "reserved" with the delta to the new fgraph frame */
	val = (FGRAPH_TYPE_RESERVED << FGRAPH_TYPE_SHIFT) | FGRAPH_FRAME_OFFSET;

	/*
	 * We must make sure the ret_stack is tested before we read
	 * anything else.
	 */
	smp_rmb();

	/*
	 * Check if there's room on the shadow stack to fit a fraph frame
	 * and a bitmap word.
	 */
	if (current->curr_ret_stack + FGRAPH_FRAME_OFFSET + 1 >= SHADOW_STACK_MAX_OFFSET) {
		atomic_inc(&current->trace_overrun);
		return -EBUSY;
	}

	offset = READ_ONCE(current->curr_ret_stack);
	ret_stack = RET_STACK(current, offset);
	offset += FGRAPH_FRAME_OFFSET;

	/* ret offset = FGRAPH_FRAME_OFFSET ; type = reserved */
	current->ret_stack[offset] = val;
	ret_stack->ret = ret;
	/*
	 * The unwinders expect curr_ret_stack to point to either zero
	 * or an offset where to find the next ret_stack. Even though the
	 * ret stack might be bogus, we want to write the ret and the
	 * offset to find the ret_stack before we increment the stack point.
	 * If an interrupt comes in now before we increment the curr_ret_stack
	 * it may blow away what we wrote. But that's fine, because the
	 * offset will still be correct (even though the 'ret' won't be).
	 * What we worry about is the offset being correct after we increment
	 * the curr_ret_stack and before we update that offset, as if an
	 * interrupt comes in and does an unwind stack dump, it will need
	 * at least a correct offset!
	 */
	barrier();
	WRITE_ONCE(current->curr_ret_stack, offset + 1);
	/*
	 * This next barrier is to ensure that an interrupt coming in
	 * will not corrupt what we are about to write.
	 */
	barrier();

	/* Still keep it reserved even if an interrupt came in */
	current->ret_stack[offset] = val;

	ret_stack->ret = ret;
	ret_stack->func = func;
#ifdef HAVE_FUNCTION_GRAPH_FP_TEST
	ret_stack->fp = frame_pointer;
#endif
	ret_stack->retp = retp;
	return offset;
}

/*
 * Not all archs define MCOUNT_INSN_SIZE which is used to look for direct
 * functions. But those archs currently don't support direct functions
 * anyway, and ftrace_find_rec_direct() is just a stub for them.
 * Define MCOUNT_INSN_SIZE to keep those archs compiling.
 */
#ifndef MCOUNT_INSN_SIZE
/* Make sure this only works without direct calls */
# ifdef CONFIG_DYNAMIC_FTRACE_WITH_DIRECT_CALLS
#  error MCOUNT_INSN_SIZE not defined with direct calls enabled
# endif
# define MCOUNT_INSN_SIZE 0
#endif

/* If the caller does not use ftrace, call this function. */
int function_graph_enter(unsigned long ret, unsigned long func,
			 unsigned long frame_pointer, unsigned long *retp)
{
	struct ftrace_graph_ent trace;
	unsigned long bitmap = 0;
	int offset;
	int i;

	trace.func = func;
	trace.depth = ++current->curr_ret_depth;

	offset = ftrace_push_return_trace(ret, func, frame_pointer, retp, 0);
	if (offset < 0)
		goto out;

#ifdef CONFIG_HAVE_STATIC_CALL
	if (static_branch_likely(&fgraph_do_direct)) {
		int save_curr_ret_stack = current->curr_ret_stack;

		if (static_call(fgraph_func)(&trace, fgraph_direct_gops))
			bitmap |= BIT(fgraph_direct_gops->idx);
		else
			/* Clear out any saved storage */
			current->curr_ret_stack = save_curr_ret_stack;
	} else
#endif
	{
		for_each_set_bit(i, &fgraph_array_bitmask,
					 sizeof(fgraph_array_bitmask) * BITS_PER_BYTE) {
			struct fgraph_ops *gops = READ_ONCE(fgraph_array[i]);
			int save_curr_ret_stack;

			if (gops == &fgraph_stub)
				continue;

			save_curr_ret_stack = current->curr_ret_stack;
			if (ftrace_ops_test(&gops->ops, func, NULL) &&
			    gops->entryfunc(&trace, gops))
				bitmap |= BIT(i);
			else
				/* Clear out any saved storage */
				current->curr_ret_stack = save_curr_ret_stack;
		}
	}

	if (!bitmap)
		goto out_ret;

	/*
	 * Since this function uses fgraph_idx = 0 as a tail-call checking
	 * flag, set that bit always.
	 */
	set_bitmap(current, offset, bitmap | BIT(0));

	return 0;
 out_ret:
	current->curr_ret_stack -= FGRAPH_FRAME_OFFSET + 1;
 out:
	current->curr_ret_depth--;
	return -EBUSY;
}

/* Retrieve a function return address to the trace stack on thread info.*/
static struct ftrace_ret_stack *
ftrace_pop_return_trace(struct ftrace_graph_ret *trace, unsigned long *ret,
			unsigned long frame_pointer, int *offset)
{
	struct ftrace_ret_stack *ret_stack;

	ret_stack = get_ret_stack(current, current->curr_ret_stack, offset);

	if (unlikely(!ret_stack)) {
		ftrace_graph_stop();
		WARN(1, "Bad function graph ret_stack pointer: %d",
		     current->curr_ret_stack);
		/* Might as well panic, otherwise we have no where to go */
		*ret = (unsigned long)panic;
		return NULL;
	}

#ifdef HAVE_FUNCTION_GRAPH_FP_TEST
	/*
	 * The arch may choose to record the frame pointer used
	 * and check it here to make sure that it is what we expect it
	 * to be. If gcc does not set the place holder of the return
	 * address in the frame pointer, and does a copy instead, then
	 * the function graph trace will fail. This test detects this
	 * case.
	 *
	 * Currently, x86_32 with optimize for size (-Os) makes the latest
	 * gcc do the above.
	 *
	 * Note, -mfentry does not use frame pointers, and this test
	 *  is not needed if CC_USING_FENTRY is set.
	 */
	if (unlikely(ret_stack->fp != frame_pointer)) {
		ftrace_graph_stop();
		WARN(1, "Bad frame pointer: expected %lx, received %lx\n"
		     "  from func %ps return to %lx\n",
		     ret_stack->fp,
		     frame_pointer,
		     (void *)ret_stack->func,
		     ret_stack->ret);
		*ret = (unsigned long)panic;
		return NULL;
	}
#endif

	*offset += FGRAPH_FRAME_OFFSET;
	*ret = ret_stack->ret;
	trace->func = ret_stack->func;
	trace->overrun = atomic_read(&current->trace_overrun);
	trace->depth = current->curr_ret_depth;
	/*
	 * We still want to trace interrupts coming in if
	 * max_depth is set to 1. Make sure the decrement is
	 * seen before ftrace_graph_return.
	 */
	barrier();

	return ret_stack;
}

/*
 * Hibernation protection.
 * The state of the current task is too much unstable during
 * suspend/restore to disk. We want to protect against that.
 */
static int
ftrace_suspend_notifier_call(struct notifier_block *bl, unsigned long state,
							void *unused)
{
	switch (state) {
	case PM_HIBERNATION_PREPARE:
		pause_graph_tracing();
		break;

	case PM_POST_HIBERNATION:
		unpause_graph_tracing();
		break;
	}
	return NOTIFY_DONE;
}

static struct notifier_block ftrace_suspend_notifier = {
	.notifier_call = ftrace_suspend_notifier_call,
};

/* fgraph_ret_regs is not defined without CONFIG_FUNCTION_GRAPH_RETVAL */
struct fgraph_ret_regs;

/*
 * Send the trace to the ring-buffer.
 * @return the original return address.
 */
static unsigned long __ftrace_return_to_handler(struct fgraph_ret_regs *ret_regs,
						unsigned long frame_pointer)
{
	struct ftrace_ret_stack *ret_stack;
	struct ftrace_graph_ret trace;
	unsigned long bitmap;
	unsigned long ret;
	int offset;
	int i;

	ret_stack = ftrace_pop_return_trace(&trace, &ret, frame_pointer, &offset);

	if (unlikely(!ret_stack)) {
		ftrace_graph_stop();
		WARN_ON(1);
		/* Might as well panic. What else to do? */
		return (unsigned long)panic;
	}

	trace.rettime = trace_clock_local();
#ifdef CONFIG_FUNCTION_GRAPH_RETVAL
	trace.retval = fgraph_ret_regs_return_value(ret_regs);
#endif

	bitmap = get_bitmap_bits(current, offset);

#ifdef CONFIG_HAVE_STATIC_CALL
	if (static_branch_likely(&fgraph_do_direct)) {
		if (test_bit(fgraph_direct_gops->idx, &bitmap))
			static_call(fgraph_retfunc)(&trace, fgraph_direct_gops);
	} else
#endif
	{
		for_each_set_bit(i, &bitmap, sizeof(bitmap) * BITS_PER_BYTE) {
			struct fgraph_ops *gops = READ_ONCE(fgraph_array[i]);

			if (gops == &fgraph_stub)
				continue;

			gops->retfunc(&trace, gops);
		}
	}

	/*
	 * The ftrace_graph_return() may still access the current
	 * ret_stack structure, we need to make sure the update of
	 * curr_ret_stack is after that.
	 */
	barrier();
	current->curr_ret_stack = offset - FGRAPH_FRAME_OFFSET;

	current->curr_ret_depth--;
	return ret;
}

/*
 * After all architecures have selected HAVE_FUNCTION_GRAPH_RETVAL, we can
 * leave only ftrace_return_to_handler(ret_regs).
 */
#ifdef CONFIG_HAVE_FUNCTION_GRAPH_RETVAL
unsigned long ftrace_return_to_handler(struct fgraph_ret_regs *ret_regs)
{
	return __ftrace_return_to_handler(ret_regs,
				fgraph_ret_regs_frame_pointer(ret_regs));
}
#else
unsigned long ftrace_return_to_handler(unsigned long frame_pointer)
{
	return __ftrace_return_to_handler(NULL, frame_pointer);
}
#endif

/**
 * ftrace_graph_get_ret_stack - return the entry of the shadow stack
 * @task: The task to read the shadow stack from.
 * @idx: Index down the shadow stack
 *
 * Return the ret_struct on the shadow stack of the @task at the
 * call graph at @idx starting with zero. If @idx is zero, it
 * will return the last saved ret_stack entry. If it is greater than
 * zero, it will return the corresponding ret_stack for the depth
 * of saved return addresses.
 */
struct ftrace_ret_stack *
ftrace_graph_get_ret_stack(struct task_struct *task, int idx)
{
	struct ftrace_ret_stack *ret_stack = NULL;
	int offset = task->curr_ret_stack;

	if (offset < 0)
		return NULL;

	do {
		ret_stack = get_ret_stack(task, offset, &offset);
	} while (ret_stack && --idx >= 0);

	return ret_stack;
}

/**
 * ftrace_graph_top_ret_addr - return the top return address in the shadow stack
 * @task: The task to read the shadow stack from.
 *
 * Return the first return address on the shadow stack of the @task, which is
 * not the fgraph's return_to_handler.
 */
unsigned long ftrace_graph_top_ret_addr(struct task_struct *task)
{
	unsigned long return_handler = (unsigned long)dereference_kernel_function_descriptor(return_to_handler);
	struct ftrace_ret_stack *ret_stack = NULL;
	int offset = task->curr_ret_stack;

	if (offset < 0)
		return 0;

	do {
		ret_stack = get_ret_stack(task, offset, &offset);
	} while (ret_stack && ret_stack->ret == return_handler);

	return ret_stack ? ret_stack->ret : 0;
}

/**
 * ftrace_graph_ret_addr - return the original value of the return address
 * @task: The task the unwinder is being executed on
 * @idx: An initialized pointer to the next stack index to use
 * @ret: The current return address (likely pointing to return_handler)
 * @retp: The address on the stack of the current return location
 *
 * This function can be called by stack unwinding code to convert a found stack
 * return address (@ret) to its original value, in case the function graph
 * tracer has modified it to be 'return_to_handler'.  If the address hasn't
 * been modified, the unchanged value of @ret is returned.
 *
 * @idx holds the last index used to know where to start from. It should be
 * initialized to zero for the first iteration as that will mean to start
 * at the top of the shadow stack. If the location is found, this pointer
 * will be assigned that location so that if called again, it will continue
 * where it left off.
 *
 * @retp is a pointer to the return address on the stack.
 */
unsigned long ftrace_graph_ret_addr(struct task_struct *task, int *idx,
				    unsigned long ret, unsigned long *retp)
{
	struct ftrace_ret_stack *ret_stack;
	unsigned long return_handler = (unsigned long)dereference_kernel_function_descriptor(return_to_handler);
	int i;

	if (ret != return_handler)
		return ret;

	if (!idx)
		return ret;

	i = *idx ? : task->curr_ret_stack;
	while (i > 0) {
		ret_stack = get_ret_stack(task, i, &i);
		if (!ret_stack)
			break;
		/*
		 * For the tail-call, there would be 2 or more ftrace_ret_stacks on
		 * the ret_stack, which records "return_to_handler" as the return
		 * address except for the last one.
		 * But on the real stack, there should be 1 entry because tail-call
		 * reuses the return address on the stack and jump to the next function.
		 * Thus we will continue to find real return address.
		 */
		if (ret_stack->retp == retp &&
		    ret_stack->ret != return_handler) {
			*idx = i;
			return ret_stack->ret;
		}
	}

	return ret;
}

static struct ftrace_ops graph_ops = {
	.func			= ftrace_graph_func,
	.flags			= FTRACE_OPS_GRAPH_STUB,
#ifdef FTRACE_GRAPH_TRAMP_ADDR
	.trampoline		= FTRACE_GRAPH_TRAMP_ADDR,
	/* trampoline_size is only needed for dynamically allocated tramps */
#endif
};

void fgraph_init_ops(struct ftrace_ops *dst_ops,
		     struct ftrace_ops *src_ops)
{
	dst_ops->flags = FTRACE_OPS_FL_PID | FTRACE_OPS_GRAPH_STUB;

#ifdef CONFIG_DYNAMIC_FTRACE
	if (src_ops) {
		dst_ops->func_hash = &src_ops->local_hash;
		mutex_init(&dst_ops->local_hash.regex_lock);
		INIT_LIST_HEAD(&dst_ops->subop_list);
		dst_ops->flags |= FTRACE_OPS_FL_INITIALIZED;
	}
#endif
}

void ftrace_graph_sleep_time_control(bool enable)
{
	fgraph_sleep_time = enable;
}

/*
 * Simply points to ftrace_stub, but with the proper protocol.
 * Defined by the linker script in linux/vmlinux.lds.h
 */
void ftrace_stub_graph(struct ftrace_graph_ret *trace, struct fgraph_ops *gops);

/* The callbacks that hook a function */
trace_func_graph_ret_t ftrace_graph_return = ftrace_stub_graph;
trace_func_graph_ent_t ftrace_graph_entry = ftrace_graph_entry_stub;

/* Try to assign a return stack array on FTRACE_RETSTACK_ALLOC_SIZE tasks. */
static int alloc_retstack_tasklist(unsigned long **ret_stack_list)
{
	int i;
	int ret = 0;
	int start = 0, end = FTRACE_RETSTACK_ALLOC_SIZE;
	struct task_struct *g, *t;

	if (WARN_ON_ONCE(!fgraph_stack_cachep))
		return -ENOMEM;

	for (i = 0; i < FTRACE_RETSTACK_ALLOC_SIZE; i++) {
		ret_stack_list[i] = kmem_cache_alloc(fgraph_stack_cachep, GFP_KERNEL);
		if (!ret_stack_list[i]) {
			start = 0;
			end = i;
			ret = -ENOMEM;
			goto free;
		}
	}

	rcu_read_lock();
	for_each_process_thread(g, t) {
		if (start == end) {
			ret = -EAGAIN;
			goto unlock;
		}

		if (t->ret_stack == NULL) {
			atomic_set(&t->trace_overrun, 0);
			ret_stack_init_task_vars(ret_stack_list[start]);
			t->curr_ret_stack = 0;
			t->curr_ret_depth = -1;
			/* Make sure the tasks see the 0 first: */
			smp_wmb();
			t->ret_stack = ret_stack_list[start++];
		}
	}

unlock:
	rcu_read_unlock();
free:
	for (i = start; i < end; i++)
		kmem_cache_free(fgraph_stack_cachep, ret_stack_list[i]);
	return ret;
}

static void
ftrace_graph_probe_sched_switch(void *ignore, bool preempt,
				struct task_struct *prev,
				struct task_struct *next,
				unsigned int prev_state)
{
	unsigned long long timestamp;

	/*
	 * Does the user want to count the time a function was asleep.
	 * If so, do not update the time stamps.
	 */
	if (fgraph_sleep_time)
		return;

	timestamp = trace_clock_local();

	prev->ftrace_timestamp = timestamp;

	/* only process tasks that we timestamped */
	if (!next->ftrace_timestamp)
		return;

	next->ftrace_sleeptime += timestamp - next->ftrace_timestamp;
}

static DEFINE_PER_CPU(unsigned long *, idle_ret_stack);

static void
graph_init_task(struct task_struct *t, unsigned long *ret_stack)
{
	atomic_set(&t->trace_overrun, 0);
	ret_stack_init_task_vars(ret_stack);
	t->ftrace_timestamp = 0;
	t->curr_ret_stack = 0;
	t->curr_ret_depth = -1;
	/* make curr_ret_stack visible before we add the ret_stack */
	smp_wmb();
	t->ret_stack = ret_stack;
}

/*
 * Allocate a return stack for the idle task. May be the first
 * time through, or it may be done by CPU hotplug online.
 */
void ftrace_graph_init_idle_task(struct task_struct *t, int cpu)
{
	t->curr_ret_stack = 0;
	t->curr_ret_depth = -1;
	/*
	 * The idle task has no parent, it either has its own
	 * stack or no stack at all.
	 */
	if (t->ret_stack)
		WARN_ON(t->ret_stack != per_cpu(idle_ret_stack, cpu));

	if (ftrace_graph_active) {
		unsigned long *ret_stack;

		if (WARN_ON_ONCE(!fgraph_stack_cachep))
			return;

		ret_stack = per_cpu(idle_ret_stack, cpu);
		if (!ret_stack) {
			ret_stack = kmem_cache_alloc(fgraph_stack_cachep, GFP_KERNEL);
			if (!ret_stack)
				return;
			per_cpu(idle_ret_stack, cpu) = ret_stack;
		}
		graph_init_task(t, ret_stack);
	}
}

/* Allocate a return stack for newly created task */
void ftrace_graph_init_task(struct task_struct *t)
{
	/* Make sure we do not use the parent ret_stack */
	t->ret_stack = NULL;
	t->curr_ret_stack = 0;
	t->curr_ret_depth = -1;

	if (ftrace_graph_active) {
		unsigned long *ret_stack;

		if (WARN_ON_ONCE(!fgraph_stack_cachep))
			return;

		ret_stack = kmem_cache_alloc(fgraph_stack_cachep, GFP_KERNEL);
		if (!ret_stack)
			return;
		graph_init_task(t, ret_stack);
	}
}

void ftrace_graph_exit_task(struct task_struct *t)
{
	unsigned long *ret_stack = t->ret_stack;

	t->ret_stack = NULL;
	/* NULL must become visible to IRQs before we free it: */
	barrier();

	if (ret_stack) {
		if (WARN_ON_ONCE(!fgraph_stack_cachep))
			return;
		kmem_cache_free(fgraph_stack_cachep, ret_stack);
	}
}

#ifdef CONFIG_DYNAMIC_FTRACE
static int fgraph_pid_func(struct ftrace_graph_ent *trace,
			   struct fgraph_ops *gops)
{
	struct trace_array *tr = gops->ops.private;
	int pid;

	if (tr) {
		pid = this_cpu_read(tr->array_buffer.data->ftrace_ignore_pid);
		if (pid == FTRACE_PID_IGNORE)
			return 0;
		if (pid != FTRACE_PID_TRACE &&
		    pid != current->pid)
			return 0;
	}

	return gops->saved_func(trace, gops);
}

void fgraph_update_pid_func(void)
{
	struct fgraph_ops *gops;
	struct ftrace_ops *op;

	if (!(graph_ops.flags & FTRACE_OPS_FL_INITIALIZED))
		return;

	list_for_each_entry(op, &graph_ops.subop_list, list) {
		if (op->flags & FTRACE_OPS_FL_PID) {
			gops = container_of(op, struct fgraph_ops, ops);
			gops->entryfunc = ftrace_pids_enabled(op) ?
				fgraph_pid_func : gops->saved_func;
			if (ftrace_graph_active == 1)
				static_call_update(fgraph_func, gops->entryfunc);
		}
	}
}
#endif

/* Allocate a return stack for each task */
static int start_graph_tracing(void)
{
	unsigned long **ret_stack_list;
	int ret, cpu;

	ret_stack_list = kcalloc(FTRACE_RETSTACK_ALLOC_SIZE,
				 sizeof(*ret_stack_list), GFP_KERNEL);

	if (!ret_stack_list)
		return -ENOMEM;

	/* The cpu_boot init_task->ret_stack will never be freed */
	for_each_online_cpu(cpu) {
		if (!idle_task(cpu)->ret_stack)
			ftrace_graph_init_idle_task(idle_task(cpu), cpu);
	}

	do {
		ret = alloc_retstack_tasklist(ret_stack_list);
	} while (ret == -EAGAIN);

	if (!ret) {
		ret = register_trace_sched_switch(ftrace_graph_probe_sched_switch, NULL);
		if (ret)
			pr_info("ftrace_graph: Couldn't activate tracepoint"
				" probe to kernel_sched_switch\n");
	}

	kfree(ret_stack_list);
	return ret;
}

static void init_task_vars(int idx)
{
	struct task_struct *g, *t;
	int cpu;

	for_each_online_cpu(cpu) {
		if (idle_task(cpu)->ret_stack)
			ret_stack_set_task_var(idle_task(cpu), idx, 0);
	}

	read_lock(&tasklist_lock);
	for_each_process_thread(g, t) {
		if (t->ret_stack)
			ret_stack_set_task_var(t, idx, 0);
	}
	read_unlock(&tasklist_lock);
}

static void ftrace_graph_enable_direct(bool enable_branch, struct fgraph_ops *gops)
{
	trace_func_graph_ent_t func = NULL;
	trace_func_graph_ret_t retfunc = NULL;
	int i;

	if (gops) {
		func = gops->entryfunc;
		retfunc = gops->retfunc;
		fgraph_direct_gops = gops;
	} else {
		for_each_set_bit(i, &fgraph_array_bitmask,
				 sizeof(fgraph_array_bitmask) * BITS_PER_BYTE) {
			func = fgraph_array[i]->entryfunc;
			retfunc = fgraph_array[i]->retfunc;
			fgraph_direct_gops = fgraph_array[i];
		}
	}
	if (WARN_ON_ONCE(!func))
		return;

	static_call_update(fgraph_func, func);
	static_call_update(fgraph_retfunc, retfunc);
	if (enable_branch)
		static_branch_disable(&fgraph_do_direct);
}

static void ftrace_graph_disable_direct(bool disable_branch)
{
	if (disable_branch)
		static_branch_disable(&fgraph_do_direct);
	static_call_update(fgraph_func, ftrace_graph_entry_stub);
	static_call_update(fgraph_retfunc, ftrace_graph_ret_stub);
	fgraph_direct_gops = &fgraph_stub;
}

/* The cpu_boot init_task->ret_stack will never be freed */
static int fgraph_cpu_init(unsigned int cpu)
{
	if (!idle_task(cpu)->ret_stack)
		ftrace_graph_init_idle_task(idle_task(cpu), cpu);
	return 0;
}

int register_ftrace_graph(struct fgraph_ops *gops)
{
	static bool fgraph_initialized;
	int command = 0;
	int ret = 0;
	int i = -1;

	guard(mutex)(&ftrace_lock);

	if (!fgraph_stack_cachep) {
		fgraph_stack_cachep = kmem_cache_create("fgraph_stack",
							SHADOW_STACK_SIZE,
							SHADOW_STACK_SIZE, 0, NULL);
		if (!fgraph_stack_cachep)
			return -ENOMEM;
	}

	if (!fgraph_initialized) {
		ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "fgraph:online",
					fgraph_cpu_init, NULL);
		if (ret < 0) {
			pr_warn("fgraph: Error to init cpu hotplug support\n");
			return ret;
		}
		fgraph_initialized = true;
		ret = 0;
	}

	if (!fgraph_array[0]) {
		/* The array must always have real data on it */
		for (i = 0; i < FGRAPH_ARRAY_SIZE; i++)
			fgraph_array[i] = &fgraph_stub;
		fgraph_lru_init();
	}

	i = fgraph_lru_alloc_index();
	if (i < 0 || WARN_ON_ONCE(fgraph_array[i] != &fgraph_stub))
		return -ENOSPC;
	gops->idx = i;

	ftrace_graph_active++;

	if (ftrace_graph_active == 2)
		ftrace_graph_disable_direct(true);

	if (ftrace_graph_active == 1) {
		ftrace_graph_enable_direct(false, gops);
		register_pm_notifier(&ftrace_suspend_notifier);
		ret = start_graph_tracing();
		if (ret)
			goto error;
		/*
		 * Some archs just test to see if these are not
		 * the default function
		 */
		ftrace_graph_return = return_run;
		ftrace_graph_entry = entry_run;
		command = FTRACE_START_FUNC_RET;
	} else {
		init_task_vars(gops->idx);
	}
	/* Always save the function, and reset at unregistering */
	gops->saved_func = gops->entryfunc;

	ret = ftrace_startup_subops(&graph_ops, &gops->ops, command);
	if (!ret)
		fgraph_array[i] = gops;

error:
	if (ret) {
		ftrace_graph_active--;
		gops->saved_func = NULL;
		fgraph_lru_release_index(i);
	}
	return ret;
}

void unregister_ftrace_graph(struct fgraph_ops *gops)
{
	int command = 0;

	guard(mutex)(&ftrace_lock);

	if (unlikely(!ftrace_graph_active))
		return;

	if (unlikely(gops->idx < 0 || gops->idx >= FGRAPH_ARRAY_SIZE ||
		     fgraph_array[gops->idx] != gops))
		return;

	if (fgraph_lru_release_index(gops->idx) < 0)
		return;

	fgraph_array[gops->idx] = &fgraph_stub;

	ftrace_graph_active--;

	if (!ftrace_graph_active)
		command = FTRACE_STOP_FUNC_RET;

	ftrace_shutdown_subops(&graph_ops, &gops->ops, command);

	if (ftrace_graph_active == 1)
		ftrace_graph_enable_direct(true, NULL);
	else if (!ftrace_graph_active)
		ftrace_graph_disable_direct(false);

	if (!ftrace_graph_active) {
		ftrace_graph_return = ftrace_stub_graph;
		ftrace_graph_entry = ftrace_graph_entry_stub;
		unregister_pm_notifier(&ftrace_suspend_notifier);
		unregister_trace_sched_switch(ftrace_graph_probe_sched_switch, NULL);
	}
	gops->saved_func = NULL;
}
