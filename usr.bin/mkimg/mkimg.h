/*-
 * Copyright (c) 2014 Juniper Networks, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _MKIMG_MKIMG_H_
#define	_MKIMG_MKIMG_H_

#include <sys/queue.h>
#include <sys/types.h>

struct part {
	TAILQ_ENTRY(part) link;
	char	*alias;		/* Partition type alias. */
	char	*contents;	/* Contents/size specification. */
	u_int	kind;		/* Content kind. */
#define	PART_UNDEF	0
#define	PART_KIND_FILE	1
#define	PART_KIND_PIPE	2
#define	PART_KIND_SIZE	3
	u_int	index;		/* Partition index (0-based). */
	uintptr_t type;		/* Scheme-specific partition type. */
	lba_t	block;		/* Block-offset of partition in image. */
	lba_t	size;		/* Size in blocks of partition. */
	char	*label;		/* Partition label. */
};

extern TAILQ_HEAD(partlisthead, part) partlist;
extern u_int nparts;

extern u_int unit_testing;
extern u_int verbose;

extern u_int ncyls;
extern u_int nheads;
extern u_int nsecs;
extern u_int secsz;	/* Logical block size. */
extern u_int blksz;	/* Physical block size. */
extern uint32_t active_partition;

static inline lba_t
round_block(lba_t n)
{
	lba_t b = blksz / secsz;
	return ((n + b - 1) & ~(b - 1));
}

static inline lba_t
round_cylinder(lba_t n)
{
	u_int cyl = nsecs * nheads;
	u_int r = n % cyl;
	return ((r == 0) ? n : n + cyl - r);
}

static inline lba_t
round_track(lba_t n)
{
	u_int r = n % nsecs;
	return ((r == 0) ? n : n + nsecs - r);
}

#if !defined(SPARSE_WRITE)
#define	sparse_write	write
#else
ssize_t sparse_write(int, const void *, size_t);
#endif

void mkimg_chs(lba_t, u_int, u_int *, u_int *, u_int *);

struct mkimg_uuid {
	uint32_t	time_low;
	uint16_t	time_mid;
	uint16_t	time_hi_and_version;
	uint8_t		clock_seq_hi_and_reserved;
	uint8_t		clock_seq_low;
	uint8_t		node[6];
};
typedef struct mkimg_uuid mkimg_uuid_t;

void mkimg_uuid(mkimg_uuid_t *);
void mkimg_uuid_enc(void *, const mkimg_uuid_t *);

#ifdef __linux__
# if !defined(__unused)
#   define __unused __attribute__ ((__unused__))
# endif
#endif

#endif /* _MKIMG_MKIMG_H_ */
