/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * User-mode machine state access
 *
 * Copyright (C) 2007 Red Hat, Inc.  All rights reserved.
 *
 * Red Hat Author: Roland McGrath.
 */

#ifndef _LINUX_REGSET_H
#define _LINUX_REGSET_H	1

#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/bug.h>
#include <linux/uaccess.h>
struct task_struct;
struct user_regset;

struct membuf {
	void *p;
	size_t left;
};

static inline int membuf_zero(struct membuf *s, size_t size)
{
	if (s->left) {
		if (size > s->left)
			size = s->left;
		memset(s->p, 0, size);
		s->p += size;
		s->left -= size;
	}
	return s->left;
}

static inline int membuf_write(struct membuf *s, const void *v, size_t size)
{
	if (s->left) {
		if (size > s->left)
			size = s->left;
		memcpy(s->p, v, size);
		s->p += size;
		s->left -= size;
	}
	return s->left;
}

static inline struct membuf membuf_at(const struct membuf *s, size_t offs)
{
	struct membuf n = *s;

	if (offs > n.left)
		offs = n.left;
	n.p += offs;
	n.left -= offs;

	return n;
}

/* current s->p must be aligned for v; v must be a scalar */
#define membuf_store(s, v)				\
({							\
	struct membuf *__s = (s);			\
        if (__s->left) {				\
		typeof(v) __v = (v);			\
		size_t __size = sizeof(__v);		\
		if (unlikely(__size > __s->left)) {	\
			__size = __s->left;		\
			memcpy(__s->p, &__v, __size);	\
		} else {				\
			*(typeof(__v + 0) *)__s->p = __v;	\
		}					\
		__s->p += __size;			\
		__s->left -= __size;			\
	}						\
	__s->left;})

/**
 * user_regset_active_fn - type of @active function in &struct user_regset
 * @target:	thread being examined
 * @regset:	regset being examined
 *
 * Return -%ENODEV if not available on the hardware found.
 * Return %0 if no interesting state in this thread.
 * Return >%0 number of @size units of interesting state.
 * Any get call fetching state beyond that number will
 * see the default initialization state for this data,
 * so a caller that knows what the default state is need
 * not copy it all out.
 * This call is optional; the pointer is %NULL if there
 * is no inexpensive check to yield a value < @n.
 */
typedef int user_regset_active_fn(struct task_struct *target,
				  const struct user_regset *regset);

typedef int user_regset_get2_fn(struct task_struct *target,
			       const struct user_regset *regset,
			       struct membuf to);

/**
 * user_regset_set_fn - type of @set function in &struct user_regset
 * @target:	thread being examined
 * @regset:	regset being examined
 * @pos:	offset into the regset data to access, in bytes
 * @count:	amount of data to copy, in bytes
 * @kbuf:	if not %NULL, a kernel-space pointer to copy from
 * @ubuf:	if @kbuf is %NULL, a user-space pointer to copy from
 *
 * Store register values.  Return %0 on success; -%EIO or -%ENODEV
 * are usual failure returns.  The @pos and @count values are in
 * bytes, but must be properly aligned.  If @kbuf is non-null, that
 * buffer is used and @ubuf is ignored.  If @kbuf is %NULL, then
 * ubuf gives a userland pointer to access directly, and an -%EFAULT
 * return value is possible.
 */
typedef int user_regset_set_fn(struct task_struct *target,
			       const struct user_regset *regset,
			       unsigned int pos, unsigned int count,
			       const void *kbuf, const void __user *ubuf);

/**
 * user_regset_writeback_fn - type of @writeback function in &struct user_regset
 * @target:	thread being examined
 * @regset:	regset being examined
 * @immediate:	zero if writeback at completion of next context switch is OK
 *
 * This call is optional; usually the pointer is %NULL.  When
 * provided, there is some user memory associated with this regset's
 * hardware, such as memory backing cached register data on register
 * window machines; the regset's data controls what user memory is
 * used (e.g. via the stack pointer value).
 *
 * Write register data back to user memory.  If the @immediate flag
 * is nonzero, it must be written to the user memory so uaccess or
 * access_process_vm() can see it when this call returns; if zero,
 * then it must be written back by the time the task completes a
 * context switch (as synchronized with wait_task_inactive()).
 * Return %0 on success or if there was nothing to do, -%EFAULT for
 * a memory problem (bad stack pointer or whatever), or -%EIO for a
 * hardware problem.
 */
typedef int user_regset_writeback_fn(struct task_struct *target,
				     const struct user_regset *regset,
				     int immediate);

/**
 * struct user_regset - accessible thread CPU state
 * @n:			Number of slots (registers).
 * @size:		Size in bytes of a slot (register).
 * @align:		Required alignment, in bytes.
 * @bias:		Bias from natural indexing.
 * @core_note_type:	ELF note @n_type value used in core dumps.
 * @get:		Function to fetch values.
 * @set:		Function to store values.
 * @active:		Function to report if regset is active, or %NULL.
 * @writeback:		Function to write data back to user memory, or %NULL.
 *
 * This data structure describes a machine resource we call a register set.
 * This is part of the state of an individual thread, not necessarily
 * actual CPU registers per se.  A register set consists of a number of
 * similar slots, given by @n.  Each slot is @size bytes, and aligned to
 * @align bytes (which is at least @size).  For dynamically-sized
 * regsets, @n must contain the maximum possible number of slots for the
 * regset.
 *
 * For backward compatibility, the @get and @set methods must pad to, or
 * accept, @n * @size bytes, even if the current regset size is smaller.
 * The precise semantics of these operations depend on the regset being
 * accessed.
 *
 * The functions to which &struct user_regset members point must be
 * called only on the current thread or on a thread that is in
 * %TASK_STOPPED or %TASK_TRACED state, that we are guaranteed will not
 * be woken up and return to user mode, and that we have called
 * wait_task_inactive() on.  (The target thread always might wake up for
 * SIGKILL while these functions are working, in which case that
 * thread's user_regset state might be scrambled.)
 *
 * The @pos argument must be aligned according to @align; the @count
 * argument must be a multiple of @size.  These functions are not
 * responsible for checking for invalid arguments.
 *
 * When there is a natural value to use as an index, @bias gives the
 * difference between the natural index and the slot index for the
 * register set.  For example, x86 GDT segment descriptors form a regset;
 * the segment selector produces a natural index, but only a subset of
 * that index space is available as a regset (the TLS slots); subtracting
 * @bias from a segment selector index value computes the regset slot.
 *
 * If nonzero, @core_note_type gives the n_type field (NT_* value)
 * of the core file note in which this regset's data appears.
 * NT_PRSTATUS is a special case in that the regset data starts at
 * offsetof(struct elf_prstatus, pr_reg) into the note data; that is
 * part of the per-machine ELF formats userland knows about.  In
 * other cases, the core file note contains exactly the whole regset
 * (@n * @size) and nothing else.  The core file note is normally
 * omitted when there is an @active function and it returns zero.
 */
struct user_regset {
	user_regset_get2_fn		*regset_get;
	user_regset_set_fn		*set;
	user_regset_active_fn		*active;
	user_regset_writeback_fn	*writeback;
	unsigned int			n;
	unsigned int 			size;
	unsigned int 			align;
	unsigned int 			bias;
	unsigned int 			core_note_type;
};

/**
 * struct user_regset_view - available regsets
 * @name:	Identifier, e.g. UTS_MACHINE string.
 * @regsets:	Array of @n regsets available in this view.
 * @n:		Number of elements in @regsets.
 * @e_machine:	ELF header @e_machine %EM_* value written in core dumps.
 * @e_flags:	ELF header @e_flags value written in core dumps.
 * @ei_osabi:	ELF header @e_ident[%EI_OSABI] value written in core dumps.
 *
 * A regset view is a collection of regsets (&struct user_regset,
 * above).  This describes all the state of a thread that can be seen
 * from a given architecture/ABI environment.  More than one view might
 * refer to the same &struct user_regset, or more than one regset
 * might refer to the same machine-specific state in the thread.  For
 * example, a 32-bit thread's state could be examined from the 32-bit
 * view or from the 64-bit view.  Either method reaches the same thread
 * register state, doing appropriate widening or truncation.
 */
struct user_regset_view {
	const char *name;
	const struct user_regset *regsets;
	unsigned int n;
	u32 e_flags;
	u16 e_machine;
	u8 ei_osabi;
};

/*
 * This is documented here rather than at the definition sites because its
 * implementation is machine-dependent but its interface is universal.
 */
/**
 * task_user_regset_view - Return the process's native regset view.
 * @tsk: a thread of the process in question
 *
 * Return the &struct user_regset_view that is native for the given process.
 * For example, what it would access when it called ptrace().
 * Throughout the life of the process, this only changes at exec.
 */
const struct user_regset_view *task_user_regset_view(struct task_struct *tsk);

static inline int user_regset_copyin(unsigned int *pos, unsigned int *count,
				     const void **kbuf,
				     const void __user **ubuf, void *data,
				     const int start_pos, const int end_pos)
{
	if (*count == 0)
		return 0;
	BUG_ON(*pos < start_pos);
	if (end_pos < 0 || *pos < end_pos) {
		unsigned int copy = (end_pos < 0 ? *count
				     : min(*count, end_pos - *pos));
		data += *pos - start_pos;
		if (*kbuf) {
			memcpy(data, *kbuf, copy);
			*kbuf += copy;
		} else if (__copy_from_user(data, *ubuf, copy))
			return -EFAULT;
		else
			*ubuf += copy;
		*pos += copy;
		*count -= copy;
	}
	return 0;
}

static inline void user_regset_copyin_ignore(unsigned int *pos,
					     unsigned int *count,
					     const void **kbuf,
					     const void __user **ubuf,
					     const int start_pos,
					     const int end_pos)
{
	if (*count == 0)
		return;
	BUG_ON(*pos < start_pos);
	if (end_pos < 0 || *pos < end_pos) {
		unsigned int copy = (end_pos < 0 ? *count
				     : min(*count, end_pos - *pos));
		if (*kbuf)
			*kbuf += copy;
		else
			*ubuf += copy;
		*pos += copy;
		*count -= copy;
	}
}

extern int regset_get(struct task_struct *target,
		      const struct user_regset *regset,
		      unsigned int size, void *data);

extern int regset_get_alloc(struct task_struct *target,
			    const struct user_regset *regset,
			    unsigned int size,
			    void **data);

extern int copy_regset_to_user(struct task_struct *target,
			       const struct user_regset_view *view,
			       unsigned int setno, unsigned int offset,
			       unsigned int size, void __user *data);

/**
 * copy_regset_from_user - store into thread's user_regset data from user memory
 * @target:	thread to be examined
 * @view:	&struct user_regset_view describing user thread machine state
 * @setno:	index in @view->regsets
 * @offset:	offset into the regset data, in bytes
 * @size:	amount of data to copy, in bytes
 * @data:	user-mode pointer to copy from
 */
static inline int copy_regset_from_user(struct task_struct *target,
					const struct user_regset_view *view,
					unsigned int setno,
					unsigned int offset, unsigned int size,
					const void __user *data)
{
	const struct user_regset *regset = &view->regsets[setno];

	if (!regset->set)
		return -EOPNOTSUPP;

	if (!access_ok(data, size))
		return -EFAULT;

	return regset->set(target, regset, offset, size, NULL, data);
}

#endif	/* <linux/regset.h> */
