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

extern void kfifo_init(struct kfifo *fifo, unsigned char *buffer,
			unsigned int size);
extern __must_check int kfifo_alloc(struct kfifo *fifo, unsigned int size,
			gfp_t gfp_mask);
extern void kfifo_free(struct kfifo *fifo);
extern unsigned int kfifo_in(struct kfifo *fifo,
				const unsigned char *from, unsigned int len);
extern __must_check unsigned int kfifo_out(struct kfifo *fifo,
				unsigned char *to, unsigned int len);

/**
 * kfifo_reset - removes the entire FIFO contents
 * @fifo: the fifo to be emptied.
 */
static inline void kfifo_reset(struct kfifo *fifo)
{
	fifo->in = fifo->out = 0;
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
		const unsigned char *from, unsigned int n, spinlock_t *lock)
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
	unsigned char *to, unsigned int n, spinlock_t *lock)
{
	unsigned long flags;
	unsigned int ret;

	spin_lock_irqsave(lock, flags);

	ret = kfifo_out(fifo, to, n);

	/*
	 * optimization: if the FIFO is empty, set the indices to 0
	 * so we don't wrap the next time
	 */
	if (fifo->in == fifo->out)
		fifo->in = fifo->out = 0;

	spin_unlock_irqrestore(lock, flags);

	return ret;
}

#endif
