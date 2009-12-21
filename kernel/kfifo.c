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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/kfifo.h>
#include <linux/log2.h>
#include <linux/uaccess.h>

static void _kfifo_init(struct kfifo *fifo, unsigned char *buffer,
		unsigned int size)
{
	fifo->buffer = buffer;
	fifo->size = size;

	kfifo_reset(fifo);
}

/**
 * kfifo_init - initialize a FIFO using a preallocated buffer
 * @fifo: the fifo to assign the buffer
 * @buffer: the preallocated buffer to be used.
 * @size: the size of the internal buffer, this have to be a power of 2.
 *
 */
void kfifo_init(struct kfifo *fifo, unsigned char *buffer, unsigned int size)
{
	/* size must be a power of 2 */
	BUG_ON(!is_power_of_2(size));

	_kfifo_init(fifo, buffer, size);
}
EXPORT_SYMBOL(kfifo_init);

/**
 * kfifo_alloc - allocates a new FIFO internal buffer
 * @fifo: the fifo to assign then new buffer
 * @size: the size of the buffer to be allocated, this have to be a power of 2.
 * @gfp_mask: get_free_pages mask, passed to kmalloc()
 *
 * This function dynamically allocates a new fifo internal buffer
 *
 * The size will be rounded-up to a power of 2.
 * The buffer will be release with kfifo_free().
 * Return 0 if no error, otherwise the an error code
 */
int kfifo_alloc(struct kfifo *fifo, unsigned int size, gfp_t gfp_mask)
{
	unsigned char *buffer;

	/*
	 * round up to the next power of 2, since our 'let the indices
	 * wrap' technique works only in this case.
	 */
	if (!is_power_of_2(size)) {
		BUG_ON(size > 0x80000000);
		size = roundup_pow_of_two(size);
	}

	buffer = kmalloc(size, gfp_mask);
	if (!buffer) {
		_kfifo_init(fifo, 0, 0);
		return -ENOMEM;
	}

	_kfifo_init(fifo, buffer, size);

	return 0;
}
EXPORT_SYMBOL(kfifo_alloc);

/**
 * kfifo_free - frees the FIFO internal buffer
 * @fifo: the fifo to be freed.
 */
void kfifo_free(struct kfifo *fifo)
{
	kfree(fifo->buffer);
}
EXPORT_SYMBOL(kfifo_free);

/**
 * kfifo_skip - skip output data
 * @fifo: the fifo to be used.
 * @len: number of bytes to skip
 */
void kfifo_skip(struct kfifo *fifo, unsigned int len)
{
	if (len < kfifo_len(fifo)) {
		__kfifo_add_out(fifo, len);
		return;
	}
	kfifo_reset_out(fifo);
}
EXPORT_SYMBOL(kfifo_skip);

/**
 * kfifo_in - puts some data into the FIFO
 * @fifo: the fifo to be used.
 * @from: the data to be added.
 * @len: the length of the data to be added.
 *
 * This function copies at most @len bytes from the @from buffer into
 * the FIFO depending on the free space, and returns the number of
 * bytes copied.
 *
 * Note that with only one concurrent reader and one concurrent
 * writer, you don't need extra locking to use these functions.
 */
unsigned int kfifo_in(struct kfifo *fifo,
			const unsigned char *from, unsigned int len)
{
	unsigned int off;
	unsigned int l;

	len = min(len, fifo->size - fifo->in + fifo->out);

	/*
	 * Ensure that we sample the fifo->out index -before- we
	 * start putting bytes into the kfifo.
	 */

	smp_mb();

	off = __kfifo_off(fifo, fifo->in);

	/* first put the data starting from fifo->in to buffer end */
	l = min(len, fifo->size - off);
	memcpy(fifo->buffer + off, from, l);

	/* then put the rest (if any) at the beginning of the buffer */
	memcpy(fifo->buffer, from + l, len - l);

	__kfifo_add_in(fifo, len);

	return len;
}
EXPORT_SYMBOL(kfifo_in);

/**
 * kfifo_out - gets some data from the FIFO
 * @fifo: the fifo to be used.
 * @to: where the data must be copied.
 * @len: the size of the destination buffer.
 *
 * This function copies at most @len bytes from the FIFO into the
 * @to buffer and returns the number of copied bytes.
 *
 * Note that with only one concurrent reader and one concurrent
 * writer, you don't need extra locking to use these functions.
 */
unsigned int kfifo_out(struct kfifo *fifo,
			 unsigned char *to, unsigned int len)
{
	unsigned int off;
	unsigned int l;

	len = min(len, fifo->in - fifo->out);

	/*
	 * Ensure that we sample the fifo->in index -before- we
	 * start removing bytes from the kfifo.
	 */

	smp_rmb();

	off = __kfifo_off(fifo, fifo->out);

	/* first get the data from fifo->out until the end of the buffer */
	l = min(len, fifo->size - off);
	memcpy(to, fifo->buffer + off, l);

	/* then get the rest (if any) from the beginning of the buffer */
	memcpy(to + l, fifo->buffer, len - l);

	__kfifo_add_out(fifo, len);

	return len;
}
EXPORT_SYMBOL(kfifo_out);

/**
 * kfifo_from_user - puts some data from user space into the FIFO
 * @fifo: the fifo to be used.
 * @from: pointer to the data to be added.
 * @len: the length of the data to be added.
 *
 * This function copies at most @len bytes from the @from into the
 * FIFO depending and returns the number of copied bytes.
 *
 * Note that with only one concurrent reader and one concurrent
 * writer, you don't need extra locking to use these functions.
 */
unsigned int kfifo_from_user(struct kfifo *fifo,
	const void __user *from, unsigned int len)
{
	unsigned int off;
	unsigned int l;
	int ret;

	len = min(len, fifo->size - fifo->in + fifo->out);

	/*
	 * Ensure that we sample the fifo->out index -before- we
	 * start putting bytes into the kfifo.
	 */

	smp_mb();

	off = __kfifo_off(fifo, fifo->in);

	/* first put the data starting from fifo->in to buffer end */
	l = min(len, fifo->size - off);
	ret = copy_from_user(fifo->buffer + off, from, l);

	if (unlikely(ret))
		return l - ret;

	/* then put the rest (if any) at the beginning of the buffer */
	ret = copy_from_user(fifo->buffer, from + l, len - l);

	if (unlikely(ret))
		return len - ret;

	__kfifo_add_in(fifo, len);

	return len;
}
EXPORT_SYMBOL(kfifo_from_user);

/**
 * kfifo_to_user - gets data from the FIFO and write it to user space
 * @fifo: the fifo to be used.
 * @to: where the data must be copied.
 * @len: the size of the destination buffer.
 *
 * This function copies at most @len bytes from the FIFO into the
 * @to buffer and returns the number of copied bytes.
 *
 * Note that with only one concurrent reader and one concurrent
 * writer, you don't need extra locking to use these functions.
 */
unsigned int kfifo_to_user(struct kfifo *fifo,
	void __user *to, unsigned int len)
{
	unsigned int off;
	unsigned int l;
	int ret;

	len = min(len, fifo->in - fifo->out);

	/*
	 * Ensure that we sample the fifo->in index -before- we
	 * start removing bytes from the kfifo.
	 */

	smp_rmb();

	off = __kfifo_off(fifo, fifo->out);

	/* first get the data from fifo->out until the end of the buffer */
	l = min(len, fifo->size - off);
	ret = copy_to_user(to, fifo->buffer + off, l);

	if (unlikely(ret))
		return l - ret;

	/* then get the rest (if any) from the beginning of the buffer */
	ret = copy_to_user(to + l, fifo->buffer, len - l);

	if (unlikely(ret))
		return len - ret;

	__kfifo_add_out(fifo, len);

	return len;
}
EXPORT_SYMBOL(kfifo_to_user);

