/*
 * A generic kernel FIFO implementation.
 *
 * Copyright (C) 2009 Stefani Seibold <stefani@seibold.net>
 * Copyright (C) 2004 Stelian Pop <stelian@popies.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

/*
 * Howto porting drivers to the new generic fifo API:
 *
 * - Modify the declaration of the "struct kfifo *" object into a
 *   in-place "struct kfifo" object
 * - Init the in-place object with kfifo_alloc() or kfifo_init()
 *   Note: The address of the in-place "struct kfifo" object must be
 *   passed as the first argument to this functions
 * - Replace the use of __kfifo_put into kfifo_in and __kfifo_get
 *   into kfifo_out
 * - Replace the use of kfifo_put into kfifo_in_locked and kfifo_get
 *   into kfifo_out_locked
 *   Note: the spinlock pointer formerly passed to kfifo_init/kfifo_alloc
 *   must be passed now to the kfifo_in_locked and kfifo_out_locked
 *   as the last parameter.
 * - All formerly name __kfifo_* functions has been renamed into kfifo_*
 */

#ifndef _LINUX_KFIFO_H
#define _LINUX_KFIFO_H

#include <linux/kernel.h>
#include <linux/spinlock.h>

struct kfifo {
	unsigned char *buffer;	/* the buffer holding the data */
	unsigned int size;	/* the size of the allocated buffer */
	unsigned int in;	/* data is added at offset (in % size) */
	unsigned int out;	/* data is extracted from off. (out % size) */
};

/*
 * Macros for declaration and initialization of the kfifo datatype
 */

/* helper macro */
#define __kfifo_initializer(s, b) \
	(struct kfifo) { \
		.size	= s, \
		.in	= 0, \
		.out	= 0, \
		.buffer = b \
	}

/**
 * DECLARE_KFIFO - macro to declare a kfifo and the associated buffer
 * @name: name of the declared kfifo datatype
 * @size: size of the fifo buffer. Must be a power of two.
 *
 * Note1: the macro can be used inside struct or union declaration
 * Note2: the macro creates two objects:
 *  A kfifo object with the given name and a buffer for the kfifo
 *  object named name##kfifo_buffer
 */
#define DECLARE_KFIFO(name, size) \
union { \
	struct kfifo name; \
	unsigned char name##kfifo_buffer[size + sizeof(struct kfifo)]; \
}

/**
 * INIT_KFIFO - Initialize a kfifo declared by DECLARE_KFIFO
 * @name: name of the declared kfifo datatype
 */
#define INIT_KFIFO(name) \
	name = __kfifo_initializer(sizeof(name##kfifo_buffer) - \
				sizeof(struct kfifo), name##kfifo_buffer)

/**
 * DEFINE_KFIFO - macro to define and initialize a kfifo
 * @name: name of the declared kfifo datatype
 * @size: size of the fifo buffer. Must be a power of two.
 *
 * Note1: the macro can be used for global and local kfifo data type variables
 * Note2: the macro creates two objects:
 *  A kfifo object with the given name and a buffer for the kfifo
 *  object named name##kfifo_buffer
 */
#define DEFINE_KFIFO(name, size) \
	unsigned char name##kfifo_buffer[size]; \
	struct kfifo name = __kfifo_initializer(size, name##kfifo_buffer)

#undef __kfifo_initializer

extern void kfifo_init(struct kfifo *fifo, void *buffer,
			unsigned int size);
extern __must_check int kfifo_alloc(struct kfifo *fifo, unsigned int size,
			gfp_t gfp_mask);
extern void kfifo_free(struct kfifo *fifo);
extern unsigned int kfifo_in(struct kfifo *fifo,
				const void *from, unsigned int len);
extern __must_check unsigned int kfifo_out(struct kfifo *fifo,
				void *to, unsigned int len);
extern __must_check unsigned int kfifo_out_peek(struct kfifo *fifo,
				void *to, unsigned int len, unsigned offset);

/**
 * kfifo_initialized - Check if kfifo is initialized.
 * @fifo: fifo to check
 * Return %true if FIFO is initialized, otherwise %false.
 * Assumes the fifo was 0 before.
 */
static inline bool kfifo_initialized(struct kfifo *fifo)
{
	return fifo->buffer != NULL;
}

/**
 * kfifo_reset - removes the entire FIFO contents
 * @fifo: the fifo to be emptied.
 */
static inline void kfifo_reset(struct kfifo *fifo)
{
	fifo->in = fifo->out = 0;
}

/**
 * kfifo_reset_out - skip FIFO contents
 * @fifo: the fifo to be emptied.
 */
static inline void kfifo_reset_out(struct kfifo *fifo)
{
	smp_mb();
	fifo->out = fifo->in;
}

/**
 * kfifo_size - returns the size of the fifo in bytes
 * @fifo: the fifo to be used.
 */
static inline __must_check unsigned int kfifo_size(struct kfifo *fifo)
{
	return fifo->size;
}

/**
 * kfifo_len - returns the number of used bytes in the FIFO
 * @fifo: the fifo to be used.
 */
static inline unsigned int kfifo_len(struct kfifo *fifo)
{
	register unsigned int	out;

	out = fifo->out;
	smp_rmb();
	return fifo->in - out;
}

/**
 * kfifo_is_empty - returns true if the fifo is empty
 * @fifo: the fifo to be used.
 */
static inline __must_check int kfifo_is_empty(struct kfifo *fifo)
{
	return fifo->in == fifo->out;
}

/**
 * kfifo_is_full - returns true if the fifo is full
 * @fifo: the fifo to be used.
 */
static inline __must_check int kfifo_is_full(struct kfifo *fifo)
{
	return kfifo_len(fifo) == kfifo_size(fifo);
}

/**
 * kfifo_avail - returns the number of bytes available in the FIFO
 * @fifo: the fifo to be used.
 */
static inline __must_check unsigned int kfifo_avail(struct kfifo *fifo)
{
	return kfifo_size(fifo) - kfifo_len(fifo);
}

/**
 * kfifo_in_locked - puts some data into the FIFO using a spinlock for locking
 * @fifo: the fifo to be used.
 * @from: the data to be added.
 * @n: the length of the data to be added.
 * @lock: pointer to the spinlock to use for locking.
 *
 * This function copies at most @len bytes from the @from buffer into
 * the FIFO depending on the free space, and returns the number of
 * bytes copied.
 */
static inline unsigned int kfifo_in_locked(struct kfifo *fifo,
		const void *from, unsigned int n, spinlock_t *lock)
{
	unsigned long flags;
	unsigned int ret;

	spin_lock_irqsave(lock, flags);

	ret = kfifo_in(fifo, from, n);

	spin_unlock_irqrestore(lock, flags);

	return ret;
}

/**
 * kfifo_out_locked - gets some data from the FIFO using a spinlock for locking
 * @fifo: the fifo to be used.
 * @to: where the data must be copied.
 * @n: the size of the destination buffer.
 * @lock: pointer to the spinlock to use for locking.
 *
 * This function copies at most @len bytes from the FIFO into the
 * @to buffer and returns the number of copied bytes.
 */
static inline __must_check unsigned int kfifo_out_locked(struct kfifo *fifo,
	void *to, unsigned int n, spinlock_t *lock)
{
	unsigned long flags;
	unsigned int ret;

	spin_lock_irqsave(lock, flags);

	ret = kfifo_out(fifo, to, n);

	spin_unlock_irqrestore(lock, flags);

	return ret;
}

extern void kfifo_skip(struct kfifo *fifo, unsigned int len);

extern __must_check int kfifo_from_user(struct kfifo *fifo,
	const void __user *from, unsigned int n, unsigned *lenout);

extern __must_check int kfifo_to_user(struct kfifo *fifo,
	void __user *to, unsigned int n, unsigned *lenout);

/*
 * __kfifo_add_out internal helper function for updating the out offset
 */
static inline void __kfifo_add_out(struct kfifo *fifo,
				unsigned int off)
{
	smp_mb();
	fifo->out += off;
}

/*
 * __kfifo_add_in internal helper function for updating the in offset
 */
static inline void __kfifo_add_in(struct kfifo *fifo,
				unsigned int off)
{
	smp_wmb();
	fifo->in += off;
}

/*
 * __kfifo_off internal helper function for calculating the index of a
 * given offeset
 */
static inline unsigned int __kfifo_off(struct kfifo *fifo, unsigned int off)
{
	return off & (fifo->size - 1);
}

/*
 * __kfifo_peek_n internal helper function for determinate the length of
 * the next record in the fifo
 */
static inline unsigned int __kfifo_peek_n(struct kfifo *fifo,
				unsigned int recsize)
{
#define __KFIFO_GET(fifo, off, shift) \
	((fifo)->buffer[__kfifo_off((fifo), (fifo)->out+(off))] << (shift))

	unsigned int l;

	l = __KFIFO_GET(fifo, 0, 0);

	if (--recsize)
		l |= __KFIFO_GET(fifo, 1, 8);

	return l;
#undef	__KFIFO_GET
}

/*
 * __kfifo_poke_n internal helper function for storing the length of
 * the next record into the fifo
 */
static inline void __kfifo_poke_n(struct kfifo *fifo,
			unsigned int recsize, unsigned int n)
{
#define __KFIFO_PUT(fifo, off, val, shift) \
		( \
		(fifo)->buffer[__kfifo_off((fifo), (fifo)->in+(off))] = \
		(unsigned char)((val) >> (shift)) \
		)

	__KFIFO_PUT(fifo, 0, n, 0);

	if (--recsize)
		__KFIFO_PUT(fifo, 1, n, 8);
#undef	__KFIFO_PUT
}

/*
 * __kfifo_in_... internal functions for put date into the fifo
 * do not call it directly, use kfifo_in_rec() instead
 */
extern unsigned int __kfifo_in_n(struct kfifo *fifo,
	const void *from, unsigned int n, unsigned int recsize);

extern unsigned int __kfifo_in_generic(struct kfifo *fifo,
	const void *from, unsigned int n, unsigned int recsize);

static inline unsigned int __kfifo_in_rec(struct kfifo *fifo,
	const void *from, unsigned int n, unsigned int recsize)
{
	unsigned int ret;

	ret = __kfifo_in_n(fifo, from, n, recsize);

	if (likely(ret == 0)) {
		if (recsize)
			__kfifo_poke_n(fifo, recsize, n);
		__kfifo_add_in(fifo, n + recsize);
	}
	return ret;
}

/**
 * kfifo_in_rec - puts some record data into the FIFO
 * @fifo: the fifo to be used.
 * @from: the data to be added.
 * @n: the length of the data to be added.
 * @recsize: size of record field
 *
 * This function copies @n bytes from the @from into the FIFO and returns
 * the number of bytes which cannot be copied.
 * A returned value greater than the @n value means that the record doesn't
 * fit into the buffer.
 *
 * Note that with only one concurrent reader and one concurrent
 * writer, you don't need extra locking to use these functions.
 */
static inline __must_check unsigned int kfifo_in_rec(struct kfifo *fifo,
	void *from, unsigned int n, unsigned int recsize)
{
	if (!__builtin_constant_p(recsize))
		return __kfifo_in_generic(fifo, from, n, recsize);
	return __kfifo_in_rec(fifo, from, n, recsize);
}

/*
 * __kfifo_out_... internal functions for get date from the fifo
 * do not call it directly, use kfifo_out_rec() instead
 */
extern unsigned int __kfifo_out_n(struct kfifo *fifo,
	void *to, unsigned int reclen, unsigned int recsize);

extern unsigned int __kfifo_out_generic(struct kfifo *fifo,
	void *to, unsigned int n,
	unsigned int recsize, unsigned int *total);

static inline unsigned int __kfifo_out_rec(struct kfifo *fifo,
	void *to, unsigned int n, unsigned int recsize,
	unsigned int *total)
{
	unsigned int l;

	if (!recsize) {
		l = n;
		if (total)
			*total = l;
	} else {
		l = __kfifo_peek_n(fifo, recsize);
		if (total)
			*total = l;
		if (n < l)
			return l;
	}

	return __kfifo_out_n(fifo, to, l, recsize);
}

/**
 * kfifo_out_rec - gets some record data from the FIFO
 * @fifo: the fifo to be used.
 * @to: where the data must be copied.
 * @n: the size of the destination buffer.
 * @recsize: size of record field
 * @total: pointer where the total number of to copied bytes should stored
 *
 * This function copies at most @n bytes from the FIFO to @to and returns the
 * number of bytes which cannot be copied.
 * A returned value greater than the @n value means that the record doesn't
 * fit into the @to buffer.
 *
 * Note that with only one concurrent reader and one concurrent
 * writer, you don't need extra locking to use these functions.
 */
static inline __must_check unsigned int kfifo_out_rec(struct kfifo *fifo,
	void *to, unsigned int n, unsigned int recsize,
	unsigned int *total)

{
	if (!__builtin_constant_p(recsize))
		return __kfifo_out_generic(fifo, to, n, recsize, total);
	return __kfifo_out_rec(fifo, to, n, recsize, total);
}

/*
 * __kfifo_from_user_... internal functions for transfer from user space into
 * the fifo. do not call it directly, use kfifo_from_user_rec() instead
 */
extern unsigned int __kfifo_from_user_n(struct kfifo *fifo,
	const void __user *from, unsigned int n, unsigned int recsize);

extern unsigned int __kfifo_from_user_generic(struct kfifo *fifo,
	const void __user *from, unsigned int n, unsigned int recsize);

static inline unsigned int __kfifo_from_user_rec(struct kfifo *fifo,
	const void __user *from, unsigned int n, unsigned int recsize)
{
	unsigned int ret;

	ret = __kfifo_from_user_n(fifo, from, n, recsize);

	if (likely(ret == 0)) {
		if (recsize)
			__kfifo_poke_n(fifo, recsize, n);
		__kfifo_add_in(fifo, n + recsize);
	}
	return ret;
}

/**
 * kfifo_from_user_rec - puts some data from user space into the FIFO
 * @fifo: the fifo to be used.
 * @from: pointer to the data to be added.
 * @n: the length of the data to be added.
 * @recsize: size of record field
 *
 * This function copies @n bytes from the @from into the
 * FIFO and returns the number of bytes which cannot be copied.
 *
 * If the returned value is equal or less the @n value, the copy_from_user()
 * functions has failed. Otherwise the record doesn't fit into the buffer.
 *
 * Note that with only one concurrent reader and one concurrent
 * writer, you don't need extra locking to use these functions.
 */
static inline __must_check unsigned int kfifo_from_user_rec(struct kfifo *fifo,
	const void __user *from, unsigned int n, unsigned int recsize)
{
	if (!__builtin_constant_p(recsize))
		return __kfifo_from_user_generic(fifo, from, n, recsize);
	return __kfifo_from_user_rec(fifo, from, n, recsize);
}

/*
 * __kfifo_to_user_... internal functions for transfer fifo data into user space
 * do not call it directly, use kfifo_to_user_rec() instead
 */
extern unsigned int __kfifo_to_user_n(struct kfifo *fifo,
	void __user *to, unsigned int n, unsigned int reclen,
	unsigned int recsize);

extern unsigned int __kfifo_to_user_generic(struct kfifo *fifo,
	void __user *to, unsigned int n, unsigned int recsize,
	unsigned int *total);

static inline unsigned int __kfifo_to_user_rec(struct kfifo *fifo,
	void __user *to, unsigned int n,
	unsigned int recsize, unsigned int *total)
{
	unsigned int l;

	if (!recsize) {
		l = n;
		if (total)
			*total = l;
	} else {
		l = __kfifo_peek_n(fifo, recsize);
		if (total)
			*total = l;
		if (n < l)
			return l;
	}

	return __kfifo_to_user_n(fifo, to, n, l, recsize);
}

/**
 * kfifo_to_user_rec - gets data from the FIFO and write it to user space
 * @fifo: the fifo to be used.
 * @to: where the data must be copied.
 * @n: the size of the destination buffer.
 * @recsize: size of record field
 * @total: pointer where the total number of to copied bytes should stored
 *
 * This function copies at most @n bytes from the FIFO to the @to.
 * In case of an error, the function returns the number of bytes which cannot
 * be copied.
 * If the returned value is equal or less the @n value, the copy_to_user()
 * functions has failed. Otherwise the record doesn't fit into the @to buffer.
 *
 * Note that with only one concurrent reader and one concurrent
 * writer, you don't need extra locking to use these functions.
 */
static inline __must_check unsigned int kfifo_to_user_rec(struct kfifo *fifo,
		void __user *to, unsigned int n, unsigned int recsize,
		unsigned int *total)
{
	if (!__builtin_constant_p(recsize))
		return __kfifo_to_user_generic(fifo, to, n, recsize, total);
	return __kfifo_to_user_rec(fifo, to, n, recsize, total);
}

/*
 * __kfifo_peek_... internal functions for peek into the next fifo record
 * do not call it directly, use kfifo_peek_rec() instead
 */
extern unsigned int __kfifo_peek_generic(struct kfifo *fifo,
				unsigned int recsize);

/**
 * kfifo_peek_rec - gets the size of the next FIFO record data
 * @fifo: the fifo to be used.
 * @recsize: size of record field
 *
 * This function returns the size of the next FIFO record in number of bytes
 */
static inline __must_check unsigned int kfifo_peek_rec(struct kfifo *fifo,
	unsigned int recsize)
{
	if (!__builtin_constant_p(recsize))
		return __kfifo_peek_generic(fifo, recsize);
	if (!recsize)
		return kfifo_len(fifo);
	return __kfifo_peek_n(fifo, recsize);
}

/*
 * __kfifo_skip_... internal functions for skip the next fifo record
 * do not call it directly, use kfifo_skip_rec() instead
 */
extern void __kfifo_skip_generic(struct kfifo *fifo, unsigned int recsize);

static inline void __kfifo_skip_rec(struct kfifo *fifo,
	unsigned int recsize)
{
	unsigned int l;

	if (recsize) {
		l = __kfifo_peek_n(fifo, recsize);

		if (l + recsize <= kfifo_len(fifo)) {
			__kfifo_add_out(fifo, l + recsize);
			return;
		}
	}
	kfifo_reset_out(fifo);
}

/**
 * kfifo_skip_rec - skip the next fifo out record
 * @fifo: the fifo to be used.
 * @recsize: size of record field
 *
 * This function skips the next FIFO record
 */
static inline void kfifo_skip_rec(struct kfifo *fifo,
	unsigned int recsize)
{
	if (!__builtin_constant_p(recsize))
		__kfifo_skip_generic(fifo, recsize);
	else
		__kfifo_skip_rec(fifo, recsize);
}

/**
 * kfifo_avail_rec - returns the number of bytes available in a record FIFO
 * @fifo: the fifo to be used.
 * @recsize: size of record field
 */
static inline __must_check unsigned int kfifo_avail_rec(struct kfifo *fifo,
	unsigned int recsize)
{
	unsigned int l = kfifo_size(fifo) - kfifo_len(fifo);

	return (l > recsize) ? l - recsize : 0;
}

#endif
