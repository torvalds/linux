/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1986, 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)buf.h	8.9 (Berkeley) 3/30/95
 * $FreeBSD$
 */

#ifndef _SYS_BIO_H_
#define	_SYS_BIO_H_

#include <sys/queue.h>
#include <sys/disk_zone.h>

/* bio_cmd */
#define BIO_READ	0x01	/* Read I/O data */
#define BIO_WRITE	0x02	/* Write I/O data */
#define BIO_DELETE	0x03	/* TRIM or free blocks, i.e. mark as unused */
#define BIO_GETATTR	0x04	/* Get GEOM attributes of object */
#define BIO_FLUSH	0x05	/* Commit outstanding I/O now */
#define BIO_CMD0	0x06	/* Available for local hacks */
#define BIO_CMD1	0x07	/* Available for local hacks */
#define BIO_CMD2	0x08	/* Available for local hacks */
#define BIO_ZONE	0x09	/* Zone command */

/* bio_flags */
#define BIO_ERROR	0x01	/* An error occurred processing this bio. */
#define BIO_DONE	0x02	/* This bio is finished. */
#define BIO_ONQUEUE	0x04	/* This bio is in a queue & not yet taken. */
/*
 * This bio must be executed after all previous bios in the queue have been
 * executed, and before any successive bios can be executed.
 */
#define BIO_ORDERED	0x08
#define	BIO_UNMAPPED	0x10
#define	BIO_TRANSIENT_MAPPING	0x20
#define	BIO_VLIST	0x40

#define	PRINT_BIO_FLAGS "\20\7vlist\6transient_mapping\5unmapped" \
	"\4ordered\3onqueue\2done\1error"

#ifdef _KERNEL
struct disk;
struct bio;
struct vm_map;

/* Empty classifier tag, to prevent further classification. */
#define	BIO_NOTCLASSIFIED		(void *)(~0UL)

typedef void bio_task_t(void *);

/*
 * The bio structure describes an I/O operation in the kernel.
 */
struct bio {
	uint16_t bio_cmd;		/* I/O operation. */
	uint16_t bio_flags;		/* General flags. */
	uint16_t bio_cflags;		/* Private use by the consumer. */
	uint16_t bio_pflags;		/* Private use by the provider. */
	struct cdev *bio_dev;		/* Device to do I/O on. */
	struct disk *bio_disk;		/* Valid below geom_disk.c only */
	off_t	bio_offset;		/* Offset into file. */
	long	bio_bcount;		/* Valid bytes in buffer. */
	caddr_t	bio_data;		/* Memory, superblocks, indirect etc. */
	struct vm_page **bio_ma;	/* Or unmapped. */
	int	bio_ma_offset;		/* Offset in the first page of bio_ma. */
	int	bio_ma_n;		/* Number of pages in bio_ma. */
	int	bio_error;		/* Errno for BIO_ERROR. */
	long	bio_resid;		/* Remaining I/O in bytes. */
	void	(*bio_done)(struct bio *);
	void	*bio_driver1;		/* Private use by the provider. */
	void	*bio_driver2;		/* Private use by the provider. */
	void	*bio_caller1;		/* Private use by the consumer. */
	void	*bio_caller2;		/* Private use by the consumer. */
	TAILQ_ENTRY(bio) bio_queue;	/* Disksort queue. */
	const char *bio_attribute;	/* Attribute for BIO_[GS]ETATTR */
	struct  disk_zone_args bio_zone;/* Used for BIO_ZONE */
	struct g_consumer *bio_from;	/* GEOM linkage */
	struct g_provider *bio_to;	/* GEOM linkage */
	off_t	bio_length;		/* Like bio_bcount */
	off_t	bio_completed;		/* Inverse of bio_resid */
	u_int	bio_children;		/* Number of spawned bios */
	u_int	bio_inbed;		/* Children safely home by now */
	struct bio *bio_parent;		/* Pointer to parent */
	struct bintime bio_t0;		/* Time request started */

	bio_task_t *bio_task;		/* Task_queue handler */
	void	*bio_task_arg;		/* Argument to above */

	void	*bio_classifier1;	/* Classifier tag. */
	void	*bio_classifier2;	/* Classifier tag. */

#ifdef DIAGNOSTIC
	void	*_bio_caller1;
	void	*_bio_caller2;
	uint8_t	_bio_cflags;
#endif
#if defined(BUF_TRACKING) || defined(FULL_BUF_TRACKING)
	struct buf *bio_track_bp;	/* Parent buf for tracking */
#endif

	/* XXX: these go away when bio chaining is introduced */
	daddr_t bio_pblkno;               /* physical block number */
};

struct uio;
struct devstat;

struct bio_queue_head {
	TAILQ_HEAD(bio_queue, bio) queue;
	off_t last_offset;
	struct	bio *insert_point;
	int total;
	int batched;
};

extern struct vm_map *bio_transient_map;
extern int bio_transient_maxcnt;

void biodone(struct bio *bp);
void biofinish(struct bio *bp, struct devstat *stat, int error);
int biowait(struct bio *bp, const char *wchan);

#if defined(BUF_TRACKING) || defined(FULL_BUF_TRACKING)
void biotrack_buf(struct bio *bp, const char *location);

static __inline void
biotrack(struct bio *bp, const char *location)
{

	if (bp->bio_track_bp != NULL)
		biotrack_buf(bp, location);
}
#else
static __inline void
biotrack(struct bio *bp __unused, const char *location __unused)
{
}
#endif

void bioq_disksort(struct bio_queue_head *ap, struct bio *bp);
struct bio *bioq_first(struct bio_queue_head *head);
struct bio *bioq_takefirst(struct bio_queue_head *head);
void bioq_flush(struct bio_queue_head *head, struct devstat *stp, int error);
void bioq_init(struct bio_queue_head *head);
void bioq_insert_head(struct bio_queue_head *head, struct bio *bp);
void bioq_insert_tail(struct bio_queue_head *head, struct bio *bp);
void bioq_remove(struct bio_queue_head *head, struct bio *bp);

int	physio(struct cdev *dev, struct uio *uio, int ioflag);
#define physread physio
#define physwrite physio

#endif /* _KERNEL */

#endif /* !_SYS_BIO_H_ */
