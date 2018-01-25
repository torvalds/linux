/*
 * User-mode machine state access
 *
 * Copyright (C) 2007 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License v.2.
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

/**
 * user_regset_get_fn - type of @get function in &struct user_regset
 * @target:	thread being examined
 * @regset:	regset being examined
 * @pos:	offset into the regset data to access, in bytes
 * @count:	amount of data to copy, in bytes
 * @kbuf:	if not %NULL, a kernel-space pointer to copy into
 * @ubuf:	if @kbuf is %NULL, a user-space pointer to copy into
 *
 * Fetch register values.  Return %0 on success; -%EIO or -%ENODEV
 * are usual failure returns.  The @pos and @count values are in
 * bytes, but must be properly aligned.  If @kbuf is non-null, that
 * buffer is used and @ubuf is ignored.  If @kbuf is %NULL, then
 * ubuf gives a userland pointer to access directly, and an -%EFAULT
 * return value is possible.
 */
typedef int user_regset_get_fn(struct task_struct *target,
			       const struct user_regset *regset,
			       unsigned int pos, unsigned int count,
			       void *kbuf, void __user *ubuf);

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
 * user_regset_get_size_fn - type of @get_size function in &struct user_regset
 * @target:	thread being examined
 * @regset:	regset being examined
 *
 * This call is optional; usually the pointer is %NULL.
 *
 * When provided, this function must return the current size of regset
 * data, as observed by the @get function in &struct user_regset.  The
 * value returned must be a multiple of @size.  The returned size is
 * required to be valid only until the next time (if any) @regset is
 * modified for @target.
 *
 * This function is intended for dynamically sized regsets.  A regset
 * that is statically sized does not need to implement it.
 *
 * This function should not be called directly: instead, callers should
 * call regset_size() to determine the current size of a regset.
 */
typedef unsigned int user_regset_get_size_fn(struct task_struct *target,
					     const struct user_regset *regset);

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
 * @get_size:		Function to return the regset's size, or %NULL.
 *
 * This data structure describes a machine resource we call a register set.
 * This is part of the state of an individual thread, not necessarily
 * actual CPU registers per se.  A register set consists of a number of
 * similar slots, given by @n.  Each slot is @size bytes, and aligned to
 * @align bytes (which is at least @size).  For dynamically-sized
 * regsets, @n must contain the maximum possible number of slots for the
 * regset, and @get_size must point to a function that returns the
 * current regset size.
 *
 * Callers that need to know only the current size of the regset and do
 * not care about its internal structure should call regset_size()
 * instead of inspecting @n or calling @get_size.
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
	user_regset_get_fn		*get;
	user_regset_set_fn		*set;
	user_regset_active_fn		*active;
	user_regset_writeback_fn	*writeback;
	user_regset_get_size_fn		*get_size;
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


/*
 * These are helpers for writing regset get/set functions in arch code.
 * Because @start_pos and @end_pos are always compile-time constants,
 * these are inlined into very little code though they look large.
 *
 * Use one or more calls sequentially for each chunk of regset data stored
 * contiguously in memory.  Call with constants for @start_pos and @end_pos,
 * giving the range of byte positions in the regset that data corresponds
 * to; @end_pos can be -1 if this chunk is at the end of the regset layout.
 * Each call updates the arguments to point past its chunk.
 */

static inline int user_regset_copyout(unsigned int *pos, unsigned int *count,
				      void **kbuf,
				      void __user **ubuf, const void *data,
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
			memcpy(*kbuf, data, copy);
			*kbuf += copy;
		} else if (__copy_to_user(*ubuf, data, copy))
			return -EFAULT;
		else
			*ubuf += copy;
		*pos += copy;
		*count -= copy;
	}
	return 0;
}

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

/*
 * These two parallel the two above, but for portions of a regset layout
 * that always read as all-zero or for which writes are ignored.
 */
static inline int user_regset_copyout_zero(unsigned int *pos,
					   unsigned int *count,
					   void **kbuf, void __user **ubuf,
					   const int start_pos,
					   const int end_pos)
{
	if (*count == 0)
		return 0;
	BUG_ON(*pos < start_pos);
	if (end_pos < 0 || *pos < end_pos) {
		unsigned int copy = (end_pos < 0 ? *count
				     : min(*count, end_pos - *pos));
		if (*kbuf) {
			memset(*kbuf, 0, copy);
			*kbuf += copy;
		} else if (__clear_user(*ubuf, copy))
			return -EFAULT;
		else
			*ubuf += copy;
		*pos += copy;
		*count -= copy;
	}
	return 0;
}

static inline int user_regset_copyin_ignore(unsigned int *pos,
					    unsigned int *count,
					    const void **kbuf,
					    const void __user **ubuf,
					    const int start_pos,
					    const int end_pos)
{
	if (*count == 0)
		return 0;
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
	return 0;
}

/**
 * copy_regset_to_user - fetch a thread's user_regset data into user memory
 * @target:	thread to be examined
 * @view:	&struct user_regset_view describing user thread machine state
 * @setno:	index in @view->regsets
 * @offset:	offset into the regset data, in bytes
 * @size:	amount of data to copy, in bytes
 * @data:	user-mode pointer to copy into
 */
static inline int copy_regset_to_user(struct task_struct *target,
				      const struct user_regset_view *view,
				      unsigned int setno,
				      unsigned int offset, unsigned int size,
				      void __user *data)
{
	const struct user_regset *regset = &view->regsets[setno];

	if (!regset->get)
		return -EOPNOTSUPP;

	if (!access_ok(VERIFY_WRITE, data, size))
		return -EFAULT;

	return regset->get(target, regset, offset, size, NULL, data);
}

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

	if (!access_ok(VERIFY_READ, data, size))
		return -EFAULT;

	return regset->set(target, regset, offset, size, NULL, data);
}

/**
 * regset_size - determine the current size of a regset
 * @target:	thread to be examined
 * @regset:	regset to be examined
 *
 * Note that the returned size is valid only until the next time
 * (if any) @regset is modified for @target.
 */
static inline unsigned int regset_size(struct task_struct *target,
				       const struct user_regset *regset)
{
	if (!regset->get_size)
		return regset->n * regset->size;
	else
		return regset->get_size(target, regset);
}

#endif	/* <linux/regset.h> */
