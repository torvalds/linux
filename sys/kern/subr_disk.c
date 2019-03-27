/*-
 * SPDX-License-Identifier: Beerware
 *
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <phk@FreeBSD.ORG> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
 * ----------------------------------------------------------------------------
 *
 * The bioq_disksort() (and the specification of the bioq API)
 * have been written by Luigi Rizzo and Fabio Checconi under the same
 * license as above.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_geom.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bio.h>
#include <sys/conf.h>
#include <sys/disk.h>
#include <sys/sysctl.h>
#include <geom/geom_disk.h>

static int bioq_batchsize = 0;
SYSCTL_INT(_debug, OID_AUTO, bioq_batchsize, CTLFLAG_RW,
    &bioq_batchsize, 0, "BIOQ batch size");

/*-
 * Disk error is the preface to plaintive error messages
 * about failing disk transfers.  It prints messages of the form
 * 	"hp0g: BLABLABLA cmd=read fsbn 12345 of 12344-12347"
 * blkdone should be -1 if the position of the error is unknown.
 * The message is printed with printf.
 */
void
disk_err(struct bio *bp, const char *what, int blkdone, int nl)
{
	daddr_t sn;

	if (bp->bio_dev != NULL)
		printf("%s: %s ", devtoname(bp->bio_dev), what);
	else if (bp->bio_disk != NULL)
		printf("%s%d: %s ",
		    bp->bio_disk->d_name, bp->bio_disk->d_unit, what);
	else
		printf("disk??: %s ", what);
	switch(bp->bio_cmd) {
	case BIO_READ:		printf("cmd=read "); break;
	case BIO_WRITE:		printf("cmd=write "); break;
	case BIO_DELETE:	printf("cmd=delete "); break;
	case BIO_GETATTR:	printf("cmd=getattr "); break;
	case BIO_FLUSH:		printf("cmd=flush "); break;
	default:		printf("cmd=%x ", bp->bio_cmd); break;
	}
	sn = bp->bio_pblkno;
	if (bp->bio_bcount <= DEV_BSIZE) {
		printf("fsbn %jd%s", (intmax_t)sn, nl ? "\n" : "");
		return;
	}
	if (blkdone >= 0) {
		sn += blkdone;
		printf("fsbn %jd of ", (intmax_t)sn);
	}
	printf("%jd-%jd", (intmax_t)bp->bio_pblkno,
	    (intmax_t)(bp->bio_pblkno + (bp->bio_bcount - 1) / DEV_BSIZE));
	if (nl)
		printf("\n");
}

/*
 * BIO queue implementation
 *
 * Please read carefully the description below before making any change
 * to the code, or you might change the behaviour of the data structure
 * in undesirable ways.
 *
 * A bioq stores disk I/O request (bio), normally sorted according to
 * the distance of the requested position (bio->bio_offset) from the
 * current head position (bioq->last_offset) in the scan direction, i.e.
 *
 * 	(uoff_t)(bio_offset - last_offset)
 *
 * Note that the cast to unsigned (uoff_t) is fundamental to insure
 * that the distance is computed in the scan direction.
 *
 * The main methods for manipulating the bioq are:
 *
 *   bioq_disksort()	performs an ordered insertion;
 *
 *   bioq_first()	return the head of the queue, without removing;
 *
 *   bioq_takefirst()	return and remove the head of the queue,
 *		updating the 'current head position' as
 *		bioq->last_offset = bio->bio_offset + bio->bio_length;
 *
 * When updating the 'current head position', we assume that the result of
 * bioq_takefirst() is dispatched to the device, so bioq->last_offset
 * represents the head position once the request is complete.
 *
 * If the bioq is manipulated using only the above calls, it starts
 * with a sorted sequence of requests with bio_offset >= last_offset,
 * possibly followed by another sorted sequence of requests with
 * 0 <= bio_offset < bioq->last_offset 
 *
 * NOTE: historical behaviour was to ignore bio->bio_length in the
 *	update, but its use tracks the head position in a better way.
 *	Historical behaviour was also to update the head position when
 *	the request under service is complete, rather than when the
 *	request is extracted from the queue. However, the current API
 *	has no method to update the head position; secondly, once
 *	a request has been submitted to the disk, we have no idea of
 *	the actual head position, so the final one is our best guess.
 *
 * --- Direct queue manipulation ---
 *
 * A bioq uses an underlying TAILQ to store requests, so we also
 * export methods to manipulate the TAILQ, in particular:
 *
 * bioq_insert_tail()	insert an entry at the end.
 *		It also creates a 'barrier' so all subsequent
 *		insertions through bioq_disksort() will end up
 *		after this entry;
 *
 * bioq_insert_head()	insert an entry at the head, update
 *		bioq->last_offset = bio->bio_offset so that
 *		all subsequent insertions through bioq_disksort()
 *		will end up after this entry;
 *
 * bioq_remove()	remove a generic element from the queue, act as
 *		bioq_takefirst() if invoked on the head of the queue.
 *
 * The semantic of these methods is the same as the operations
 * on the underlying TAILQ, but with additional guarantees on
 * subsequent bioq_disksort() calls. E.g. bioq_insert_tail()
 * can be useful for making sure that all previous ops are flushed
 * to disk before continuing.
 *
 * Updating bioq->last_offset on a bioq_insert_head() guarantees
 * that the bio inserted with the last bioq_insert_head() will stay
 * at the head of the queue even after subsequent bioq_disksort().
 *
 * Note that when the direct queue manipulation functions are used,
 * the queue may contain multiple inversion points (i.e. more than
 * two sorted sequences of requests).
 *
 */

void
bioq_init(struct bio_queue_head *head)
{

	TAILQ_INIT(&head->queue);
	head->last_offset = 0;
	head->insert_point = NULL;
	head->total = 0;
	head->batched = 0;
}

void
bioq_remove(struct bio_queue_head *head, struct bio *bp)
{

	if (head->insert_point == NULL) {
		if (bp == TAILQ_FIRST(&head->queue))
			head->last_offset = bp->bio_offset + bp->bio_length;
	} else if (bp == head->insert_point)
		head->insert_point = NULL;

	TAILQ_REMOVE(&head->queue, bp, bio_queue);
	head->total--;
}

void
bioq_flush(struct bio_queue_head *head, struct devstat *stp, int error)
{
	struct bio *bp;

	while ((bp = bioq_takefirst(head)) != NULL)
		biofinish(bp, stp, error);
}

void
bioq_insert_head(struct bio_queue_head *head, struct bio *bp)
{

	if (head->insert_point == NULL)
		head->last_offset = bp->bio_offset;
	TAILQ_INSERT_HEAD(&head->queue, bp, bio_queue);
	head->total++;
	head->batched = 0;
}

void
bioq_insert_tail(struct bio_queue_head *head, struct bio *bp)
{

	TAILQ_INSERT_TAIL(&head->queue, bp, bio_queue);
	head->total++;
	head->batched = 0;
	head->insert_point = bp;
	head->last_offset = bp->bio_offset;
}

struct bio *
bioq_first(struct bio_queue_head *head)
{

	return (TAILQ_FIRST(&head->queue));
}

struct bio *
bioq_takefirst(struct bio_queue_head *head)
{
	struct bio *bp;

	bp = TAILQ_FIRST(&head->queue);
	if (bp != NULL)
		bioq_remove(head, bp);
	return (bp);
}

/*
 * Compute the sorting key. The cast to unsigned is
 * fundamental for correctness, see the description
 * near the beginning of the file.
 */
static inline uoff_t
bioq_bio_key(struct bio_queue_head *head, struct bio *bp)
{

	return ((uoff_t)(bp->bio_offset - head->last_offset));
}

/*
 * Seek sort for disks.
 *
 * Sort all requests in a single queue while keeping
 * track of the current position of the disk with last_offset.
 * See above for details.
 */
void
bioq_disksort(struct bio_queue_head *head, struct bio *bp)
{
	struct bio *cur, *prev;
	uoff_t key;

	if ((bp->bio_flags & BIO_ORDERED) != 0) {
		/*
		 * Ordered transactions can only be dispatched
		 * after any currently queued transactions.  They
		 * also have barrier semantics - no transactions
		 * queued in the future can pass them.
		 */
		bioq_insert_tail(head, bp);
		return;
	}

	/*
	 * We should only sort requests of types that have concept of offset.
	 * Other types, such as BIO_FLUSH or BIO_ZONE, may imply some degree
	 * of ordering even if strict ordering is not requested explicitly.
	 */
	if (bp->bio_cmd != BIO_READ && bp->bio_cmd != BIO_WRITE &&
	    bp->bio_cmd != BIO_DELETE) {
		bioq_insert_tail(head, bp);
		return;
	}

	if (bioq_batchsize > 0 && head->batched > bioq_batchsize) {
		bioq_insert_tail(head, bp);
		return;
	}

	prev = NULL;
	key = bioq_bio_key(head, bp);
	cur = TAILQ_FIRST(&head->queue);

	if (head->insert_point) {
		prev = head->insert_point;
		cur = TAILQ_NEXT(head->insert_point, bio_queue);
	}

	while (cur != NULL && key >= bioq_bio_key(head, cur)) {
		prev = cur;
		cur = TAILQ_NEXT(cur, bio_queue);
	}

	if (prev == NULL)
		TAILQ_INSERT_HEAD(&head->queue, bp, bio_queue);
	else
		TAILQ_INSERT_AFTER(&head->queue, prev, bp, bio_queue);
	head->total++;
	head->batched++;
}
