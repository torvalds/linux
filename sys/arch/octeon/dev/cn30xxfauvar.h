/*	$OpenBSD: cn30xxfauvar.h,v 1.5 2024/05/20 23:13:33 jsg Exp $	*/

/*
 * Copyright (c) 2007 Internet Initiative Japan, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _CN30XXFAUVAR_H_
#define _CN30XXFAUVAR_H_

/* ---- API */

struct cn30xxfau_desc {
	/* offset in scratch buffer */
	size_t		fd_scroff;	/* XXX offset_t */
	/* FAU register number */
	size_t		fd_regno;	/* XXX offset_t */
};

void		cn30xxfau_op_init(struct cn30xxfau_desc *, size_t, size_t);
uint64_t	cn30xxfau_op_save(struct cn30xxfau_desc *);
void		cn30xxfau_op_restore(struct cn30xxfau_desc *, uint64_t);
int64_t		cn30xxfau_op_inc_8(struct cn30xxfau_desc *, int64_t);
int64_t		cn30xxfau_op_incwait_8(struct cn30xxfau_desc *, int);
void		cn30xxfau_op_add_8(struct cn30xxfau_desc *, int64_t);
void		cn30xxfau_op_set_8(struct cn30xxfau_desc *, int64_t);

/* ---- old API */

/* XXX */
#define OCT_FAU_REG_OQ_ADDR_INDEX (0)
#define OCT_FAU_REG_ADDR_END (256)
/* XXX */

/* XXX */
typedef enum {
	OCT_FAU_OP_SIZE_8  = 0,
	OCT_FAU_OP_SIZE_16 = 1,
	OCT_FAU_OP_SIZE_32 = 2,
	OCT_FAU_OP_SIZE_64 = 3
} fau_op_size_t;
/* XXX */

static inline void
cn30xxfau_op_iobdma(int index, uint64_t args)
{
	uint64_t value;

	value =
	    ((uint64_t)(index & 0xff) << 56) |
	    ((uint64_t)1 << 48) |
	    ((uint64_t)(CN30XXFAU_MAJORDID & 0x1f) << 43) |
	    ((uint64_t)(CN30XXFAU_SUBDID & 0x7) << 40) |
	    ((uint64_t)args & 0xfffffffffULL);
	octeon_iobdma_write_8(value);
}

/* 3.4.2 IOBDMA Operations */

/* IOBDMA Store Data for FAU Operations */

static inline void
cn30xxfau_op_iobdma_store_data(int scraddr, int incval, int tagwait,
    int size, int reg)
{
	uint64_t args;

	args =
	    ((uint64_t)(incval & 0x3fffff) << 14) |
	    ((uint64_t)(tagwait & 0x1) << 13) |  
	    ((uint64_t)(size & 0x3) << 11) |  
	    ((uint64_t)(reg & 0x7ff) << 0);
	/* use `srcaddr` as `index' */
	cn30xxfau_op_iobdma((int)((uint32_t)scraddr >> 3) /* XXX */, args);
}

static inline void
cn30xxfau_op_inc_fetch_8(struct cn30xxfau_desc *fd, int64_t v)
{
	cn30xxfau_op_iobdma_store_data(fd->fd_scroff, v, 0, OCT_FAU_OP_SIZE_64/* XXX */,
	    fd->fd_regno);
}

static inline int64_t
cn30xxfau_op_inc_read_8(struct cn30xxfau_desc *fd)
{
	octeon_synciobdma();
	return octeon_cvmseg_read_8(fd->fd_scroff);
}

#endif /* _CN30XXFAUVAR_H_ */
