/*-
 * Copyright (c) 2010 Isilon Systems, Inc.
 * Copyright (c) 2010 iX Systems, Inc.
 * Copyright (c) 2010 Panasas, Inc.
 * Copyright (c) 2013-2016 Mellanox Technologies, Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef	_LINUX_IDR_H_
#define	_LINUX_IDR_H_

#include <sys/param.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#include <linux/types.h>

#define	IDR_BITS	5
#define	IDR_SIZE	(1 << IDR_BITS)
#define	IDR_MASK	(IDR_SIZE - 1)

#define	MAX_ID_SHIFT	((sizeof(int) * NBBY) - 1)
#define	MAX_ID_BIT	(1U << MAX_ID_SHIFT)
#define	MAX_ID_MASK	(MAX_ID_BIT - 1)
#define	MAX_LEVEL	(MAX_ID_SHIFT + IDR_BITS - 1) / IDR_BITS

#define MAX_IDR_SHIFT (sizeof(int)*8 - 1)
#define MAX_IDR_BIT (1U << MAX_IDR_SHIFT)
#define MAX_IDR_MASK (MAX_IDR_BIT - 1)

struct idr_layer {
	unsigned long		bitmap;
	struct idr_layer	*ary[IDR_SIZE];
};

struct idr {
	struct mtx		lock;
	struct idr_layer	*top;
	struct idr_layer	*free;
	int			layers;
	int			next_cyclic_id;
};

/* NOTE: It is the applications responsibility to destroy the IDR */
#define	DEFINE_IDR(name)						\
	struct idr name;						\
	SYSINIT(name##_idr_sysinit, SI_SUB_DRIVERS, SI_ORDER_FIRST,	\
	    idr_init, &(name))

/* NOTE: It is the applications responsibility to destroy the IDA */
#define	DEFINE_IDA(name)						\
	struct ida name;						\
	SYSINIT(name##_ida_sysinit, SI_SUB_DRIVERS, SI_ORDER_FIRST,	\
	    ida_init, &(name))

void	idr_preload(gfp_t gfp_mask);
void	idr_preload_end(void);
void	*idr_find(struct idr *idp, int id);
void	*idr_get_next(struct idr *idp, int *nextid);
bool	idr_is_empty(struct idr *idp);
int	idr_pre_get(struct idr *idp, gfp_t gfp_mask);
int	idr_get_new(struct idr *idp, void *ptr, int *id);
int	idr_get_new_above(struct idr *idp, void *ptr, int starting_id, int *id);
void	*idr_replace(struct idr *idp, void *ptr, int id);
void	*idr_remove(struct idr *idp, int id);
void	idr_remove_all(struct idr *idp);
void	idr_destroy(struct idr *idp);
void	idr_init(struct idr *idp);
int	idr_alloc(struct idr *idp, void *ptr, int start, int end, gfp_t);
int	idr_alloc_cyclic(struct idr *idp, void *ptr, int start, int end, gfp_t);
int	idr_for_each(struct idr *idp, int (*fn)(int id, void *p, void *data), void *data);

#define	idr_for_each_entry(idp, entry, id)	\
	for ((id) = 0; ((entry) = idr_get_next(idp, &(id))) != NULL; ++(id))

#define	IDA_CHUNK_SIZE		128	/* 128 bytes per chunk */
#define	IDA_BITMAP_LONGS	(IDA_CHUNK_SIZE / sizeof(long) - 1)
#define	IDA_BITMAP_BITS		(IDA_BITMAP_LONGS * sizeof(long) * 8)

struct ida_bitmap {
	long			nr_busy;
	unsigned long		bitmap[IDA_BITMAP_LONGS];
};

struct ida {
	struct idr		idr;
	struct ida_bitmap	*free_bitmap;
};

int	ida_pre_get(struct ida *ida, gfp_t gfp_mask);
int	ida_get_new_above(struct ida *ida, int starting_id, int *p_id);
void	ida_remove(struct ida *ida, int id);
void	ida_destroy(struct ida *ida);
void	ida_init(struct ida *ida);

int	ida_simple_get(struct ida *ida, unsigned int start, unsigned int end,
    gfp_t gfp_mask);
void	ida_simple_remove(struct ida *ida, unsigned int id);

static inline void
ida_free(struct ida *ida, int id)
{

	ida_remove(ida, id);
}

static inline int
ida_get_new(struct ida *ida, int *p_id)
{

	return (ida_get_new_above(ida, 0, p_id));
}

static inline int
ida_alloc_max(struct ida *ida, unsigned int max, gfp_t gfp)
{

	return (ida_simple_get(ida, 0, max, gfp));
}

static inline bool
ida_is_empty(struct ida *ida)
{

	return (idr_is_empty(&ida->idr));
}

#endif	/* _LINUX_IDR_H_ */
