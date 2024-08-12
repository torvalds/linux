// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * A generic kernel FIFO implementation
 *
 * Copyright (C) 2009/2010 Stefani Seibold <stefani@seibold.net>
 */

#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/kfifo.h>
#include <linux/log2.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

/*
 * internal helper to calculate the unused elements in a fifo
 */
static inline unsigned int kfifo_unused(struct __kfifo *fifo)
{
	return (fifo->mask + 1) - (fifo->in - fifo->out);
}

int __kfifo_alloc(struct __kfifo *fifo, unsigned int size,
		size_t esize, gfp_t gfp_mask)
{
	/*
	 * round up to the next power of 2, since our 'let the indices
	 * wrap' technique works only in this case.
	 */
	size = roundup_pow_of_two(size);

	fifo->in = 0;
	fifo->out = 0;
	fifo->esize = esize;

	if (size < 2) {
		fifo->data = NULL;
		fifo->mask = 0;
		return -EINVAL;
	}

	fifo->data = kmalloc_array(esize, size, gfp_mask);

	if (!fifo->data) {
		fifo->mask = 0;
		return -ENOMEM;
	}
	fifo->mask = size - 1;

	return 0;
}
EXPORT_SYMBOL(__kfifo_alloc);

void __kfifo_free(struct __kfifo *fifo)
{
	kfree(fifo->data);
	fifo->in = 0;
	fifo->out = 0;
	fifo->esize = 0;
	fifo->data = NULL;
	fifo->mask = 0;
}
EXPORT_SYMBOL(__kfifo_free);

int __kfifo_init(struct __kfifo *fifo, void *buffer,
		unsigned int size, size_t esize)
{
	size /= esize;

	if (!is_power_of_2(size))
		size = rounddown_pow_of_two(size);

	fifo->in = 0;
	fifo->out = 0;
	fifo->esize = esize;
	fifo->data = buffer;

	if (size < 2) {
		fifo->mask = 0;
		return -EINVAL;
	}
	fifo->mask = size - 1;

	return 0;
}
EXPORT_SYMBOL(__kfifo_init);

static void kfifo_copy_in(struct __kfifo *fifo, const void *src,
		unsigned int len, unsigned int off)
{
	unsigned int size = fifo->mask + 1;
	unsigned int esize = fifo->esize;
	unsigned int l;

	off &= fifo->mask;
	if (esize != 1) {
		off *= esize;
		size *= esize;
		len *= esize;
	}
	l = min(len, size - off);

	memcpy(fifo->data + off, src, l);
	memcpy(fifo->data, src + l, len - l);
	/*
	 * make sure that the data in the fifo is up to date before
	 * incrementing the fifo->in index counter
	 */
	smp_wmb();
}

unsigned int __kfifo_in(struct __kfifo *fifo,
		const void *buf, unsigned int len)
{
	unsigned int l;

	l = kfifo_unused(fifo);
	if (len > l)
		len = l;

	kfifo_copy_in(fifo, buf, len, fifo->in);
	fifo->in += len;
	return len;
}
EXPORT_SYMBOL(__kfifo_in);

static void kfifo_copy_out(struct __kfifo *fifo, void *dst,
		unsigned int len, unsigned int off)
{
	unsigned int size = fifo->mask + 1;
	unsigned int esize = fifo->esize;
	unsigned int l;

	off &= fifo->mask;
	if (esize != 1) {
		off *= esize;
		size *= esize;
		len *= esize;
	}
	l = min(len, size - off);

	memcpy(dst, fifo->data + off, l);
	memcpy(dst + l, fifo->data, len - l);
	/*
	 * make sure that the data is copied before
	 * incrementing the fifo->out index counter
	 */
	smp_wmb();
}

unsigned int __kfifo_out_peek(struct __kfifo *fifo,
		void *buf, unsigned int len)
{
	unsigned int l;

	l = fifo->in - fifo->out;
	if (len > l)
		len = l;

	kfifo_copy_out(fifo, buf, len, fifo->out);
	return len;
}
EXPORT_SYMBOL(__kfifo_out_peek);

unsigned int __kfifo_out_linear(struct __kfifo *fifo,
		unsigned int *tail, unsigned int n)
{
	unsigned int size = fifo->mask + 1;
	unsigned int off = fifo->out & fifo->mask;

	if (tail)
		*tail = off;

	return min3(n, fifo->in - fifo->out, size - off);
}
EXPORT_SYMBOL(__kfifo_out_linear);

unsigned int __kfifo_out(struct __kfifo *fifo,
		void *buf, unsigned int len)
{
	len = __kfifo_out_peek(fifo, buf, len);
	fifo->out += len;
	return len;
}
EXPORT_SYMBOL(__kfifo_out);

static unsigned long kfifo_copy_from_user(struct __kfifo *fifo,
	const void __user *from, unsigned int len, unsigned int off,
	unsigned int *copied)
{
	unsigned int size = fifo->mask + 1;
	unsigned int esize = fifo->esize;
	unsigned int l;
	unsigned long ret;

	off &= fifo->mask;
	if (esize != 1) {
		off *= esize;
		size *= esize;
		len *= esize;
	}
	l = min(len, size - off);

	ret = copy_from_user(fifo->data + off, from, l);
	if (unlikely(ret))
		ret = DIV_ROUND_UP(ret + len - l, esize);
	else {
		ret = copy_from_user(fifo->data, from + l, len - l);
		if (unlikely(ret))
			ret = DIV_ROUND_UP(ret, esize);
	}
	/*
	 * make sure that the data in the fifo is up to date before
	 * incrementing the fifo->in index counter
	 */
	smp_wmb();
	*copied = len - ret * esize;
	/* return the number of elements which are not copied */
	return ret;
}

int __kfifo_from_user(struct __kfifo *fifo, const void __user *from,
		unsigned long len, unsigned int *copied)
{
	unsigned int l;
	unsigned long ret;
	unsigned int esize = fifo->esize;
	int err;

	if (esize != 1)
		len /= esize;

	l = kfifo_unused(fifo);
	if (len > l)
		len = l;

	ret = kfifo_copy_from_user(fifo, from, len, fifo->in, copied);
	if (unlikely(ret)) {
		len -= ret;
		err = -EFAULT;
	} else
		err = 0;
	fifo->in += len;
	return err;
}
EXPORT_SYMBOL(__kfifo_from_user);

static unsigned long kfifo_copy_to_user(struct __kfifo *fifo, void __user *to,
		unsigned int len, unsigned int off, unsigned int *copied)
{
	unsigned int l;
	unsigned long ret;
	unsigned int size = fifo->mask + 1;
	unsigned int esize = fifo->esize;

	off &= fifo->mask;
	if (esize != 1) {
		off *= esize;
		size *= esize;
		len *= esize;
	}
	l = min(len, size - off);

	ret = copy_to_user(to, fifo->data + off, l);
	if (unlikely(ret))
		ret = DIV_ROUND_UP(ret + len - l, esize);
	else {
		ret = copy_to_user(to + l, fifo->data, len - l);
		if (unlikely(ret))
			ret = DIV_ROUND_UP(ret, esize);
	}
	/*
	 * make sure that the data is copied before
	 * incrementing the fifo->out index counter
	 */
	smp_wmb();
	*copied = len - ret * esize;
	/* return the number of elements which are not copied */
	return ret;
}

int __kfifo_to_user(struct __kfifo *fifo, void __user *to,
		unsigned long len, unsigned int *copied)
{
	unsigned int l;
	unsigned long ret;
	unsigned int esize = fifo->esize;
	int err;

	if (esize != 1)
		len /= esize;

	l = fifo->in - fifo->out;
	if (len > l)
		len = l;
	ret = kfifo_copy_to_user(fifo, to, len, fifo->out, copied);
	if (unlikely(ret)) {
		len -= ret;
		err = -EFAULT;
	} else
		err = 0;
	fifo->out += len;
	return err;
}
EXPORT_SYMBOL(__kfifo_to_user);

static unsigned int setup_sgl_buf(struct __kfifo *fifo, struct scatterlist *sgl,
				  unsigned int data_offset, int nents,
				  unsigned int len, dma_addr_t dma)
{
	const void *buf = fifo->data + data_offset;

	if (!nents || !len)
		return 0;

	sg_set_buf(sgl, buf, len);

	if (dma != DMA_MAPPING_ERROR) {
		sg_dma_address(sgl) = dma + data_offset;
		sg_dma_len(sgl) = len;
	}

	return 1;
}

static unsigned int setup_sgl(struct __kfifo *fifo, struct scatterlist *sgl,
		int nents, unsigned int len, unsigned int off, dma_addr_t dma)
{
	unsigned int size = fifo->mask + 1;
	unsigned int esize = fifo->esize;
	unsigned int len_to_end;
	unsigned int n;

	off &= fifo->mask;
	if (esize != 1) {
		off *= esize;
		size *= esize;
		len *= esize;
	}
	len_to_end = min(len, size - off);

	n = setup_sgl_buf(fifo, sgl, off, nents, len_to_end, dma);
	n += setup_sgl_buf(fifo, sgl + n, 0, nents - n, len - len_to_end, dma);

	return n;
}

unsigned int __kfifo_dma_in_prepare(struct __kfifo *fifo,
		struct scatterlist *sgl, int nents, unsigned int len,
		dma_addr_t dma)
{
	unsigned int l;

	l = kfifo_unused(fifo);
	if (len > l)
		len = l;

	return setup_sgl(fifo, sgl, nents, len, fifo->in, dma);
}
EXPORT_SYMBOL(__kfifo_dma_in_prepare);

unsigned int __kfifo_dma_out_prepare(struct __kfifo *fifo,
		struct scatterlist *sgl, int nents, unsigned int len,
		dma_addr_t dma)
{
	unsigned int l;

	l = fifo->in - fifo->out;
	if (len > l)
		len = l;

	return setup_sgl(fifo, sgl, nents, len, fifo->out, dma);
}
EXPORT_SYMBOL(__kfifo_dma_out_prepare);

unsigned int __kfifo_max_r(unsigned int len, size_t recsize)
{
	unsigned int max = (1 << (recsize << 3)) - 1;

	if (len > max)
		return max;
	return len;
}
EXPORT_SYMBOL(__kfifo_max_r);

#define	__KFIFO_PEEK(data, out, mask) \
	((data)[(out) & (mask)])
/*
 * __kfifo_peek_n internal helper function for determinate the length of
 * the next record in the fifo
 */
static unsigned int __kfifo_peek_n(struct __kfifo *fifo, size_t recsize)
{
	unsigned int l;
	unsigned int mask = fifo->mask;
	unsigned char *data = fifo->data;

	l = __KFIFO_PEEK(data, fifo->out, mask);

	if (--recsize)
		l |= __KFIFO_PEEK(data, fifo->out + 1, mask) << 8;

	return l;
}

#define	__KFIFO_POKE(data, in, mask, val) \
	( \
	(data)[(in) & (mask)] = (unsigned char)(val) \
	)

/*
 * __kfifo_poke_n internal helper function for storing the length of
 * the record into the fifo
 */
static void __kfifo_poke_n(struct __kfifo *fifo, unsigned int n, size_t recsize)
{
	unsigned int mask = fifo->mask;
	unsigned char *data = fifo->data;

	__KFIFO_POKE(data, fifo->in, mask, n);

	if (recsize > 1)
		__KFIFO_POKE(data, fifo->in + 1, mask, n >> 8);
}

unsigned int __kfifo_len_r(struct __kfifo *fifo, size_t recsize)
{
	return __kfifo_peek_n(fifo, recsize);
}
EXPORT_SYMBOL(__kfifo_len_r);

unsigned int __kfifo_in_r(struct __kfifo *fifo, const void *buf,
		unsigned int len, size_t recsize)
{
	if (len + recsize > kfifo_unused(fifo))
		return 0;

	__kfifo_poke_n(fifo, len, recsize);

	kfifo_copy_in(fifo, buf, len, fifo->in + recsize);
	fifo->in += len + recsize;
	return len;
}
EXPORT_SYMBOL(__kfifo_in_r);

static unsigned int kfifo_out_copy_r(struct __kfifo *fifo,
	void *buf, unsigned int len, size_t recsize, unsigned int *n)
{
	*n = __kfifo_peek_n(fifo, recsize);

	if (len > *n)
		len = *n;

	kfifo_copy_out(fifo, buf, len, fifo->out + recsize);
	return len;
}

unsigned int __kfifo_out_peek_r(struct __kfifo *fifo, void *buf,
		unsigned int len, size_t recsize)
{
	unsigned int n;

	if (fifo->in == fifo->out)
		return 0;

	return kfifo_out_copy_r(fifo, buf, len, recsize, &n);
}
EXPORT_SYMBOL(__kfifo_out_peek_r);

unsigned int __kfifo_out_linear_r(struct __kfifo *fifo,
		unsigned int *tail, unsigned int n, size_t recsize)
{
	if (fifo->in == fifo->out)
		return 0;

	if (tail)
		*tail = fifo->out + recsize;

	return min(n, __kfifo_peek_n(fifo, recsize));
}
EXPORT_SYMBOL(__kfifo_out_linear_r);

unsigned int __kfifo_out_r(struct __kfifo *fifo, void *buf,
		unsigned int len, size_t recsize)
{
	unsigned int n;

	if (fifo->in == fifo->out)
		return 0;

	len = kfifo_out_copy_r(fifo, buf, len, recsize, &n);
	fifo->out += n + recsize;
	return len;
}
EXPORT_SYMBOL(__kfifo_out_r);

void __kfifo_skip_r(struct __kfifo *fifo, size_t recsize)
{
	unsigned int n;

	n = __kfifo_peek_n(fifo, recsize);
	fifo->out += n + recsize;
}
EXPORT_SYMBOL(__kfifo_skip_r);

int __kfifo_from_user_r(struct __kfifo *fifo, const void __user *from,
	unsigned long len, unsigned int *copied, size_t recsize)
{
	unsigned long ret;

	len = __kfifo_max_r(len, recsize);

	if (len + recsize > kfifo_unused(fifo)) {
		*copied = 0;
		return 0;
	}

	__kfifo_poke_n(fifo, len, recsize);

	ret = kfifo_copy_from_user(fifo, from, len, fifo->in + recsize, copied);
	if (unlikely(ret)) {
		*copied = 0;
		return -EFAULT;
	}
	fifo->in += len + recsize;
	return 0;
}
EXPORT_SYMBOL(__kfifo_from_user_r);

int __kfifo_to_user_r(struct __kfifo *fifo, void __user *to,
	unsigned long len, unsigned int *copied, size_t recsize)
{
	unsigned long ret;
	unsigned int n;

	if (fifo->in == fifo->out) {
		*copied = 0;
		return 0;
	}

	n = __kfifo_peek_n(fifo, recsize);
	if (len > n)
		len = n;

	ret = kfifo_copy_to_user(fifo, to, len, fifo->out + recsize, copied);
	if (unlikely(ret)) {
		*copied = 0;
		return -EFAULT;
	}
	fifo->out += n + recsize;
	return 0;
}
EXPORT_SYMBOL(__kfifo_to_user_r);

unsigned int __kfifo_dma_in_prepare_r(struct __kfifo *fifo,
	struct scatterlist *sgl, int nents, unsigned int len, size_t recsize,
	dma_addr_t dma)
{
	BUG_ON(!nents);

	len = __kfifo_max_r(len, recsize);

	if (len + recsize > kfifo_unused(fifo))
		return 0;

	return setup_sgl(fifo, sgl, nents, len, fifo->in + recsize, dma);
}
EXPORT_SYMBOL(__kfifo_dma_in_prepare_r);

void __kfifo_dma_in_finish_r(struct __kfifo *fifo,
	unsigned int len, size_t recsize)
{
	len = __kfifo_max_r(len, recsize);
	__kfifo_poke_n(fifo, len, recsize);
	fifo->in += len + recsize;
}
EXPORT_SYMBOL(__kfifo_dma_in_finish_r);

unsigned int __kfifo_dma_out_prepare_r(struct __kfifo *fifo,
	struct scatterlist *sgl, int nents, unsigned int len, size_t recsize,
	dma_addr_t dma)
{
	BUG_ON(!nents);

	len = __kfifo_max_r(len, recsize);

	if (len + recsize > fifo->in - fifo->out)
		return 0;

	return setup_sgl(fifo, sgl, nents, len, fifo->out + recsize, dma);
}
EXPORT_SYMBOL(__kfifo_dma_out_prepare_r);

