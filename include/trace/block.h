#ifndef _TRACE_BLOCK_H
#define _TRACE_BLOCK_H

#include <linux/blkdev.h>
#include <linux/tracepoint.h>

DECLARE_TRACE(block_rq_abort,
	TP_PROTO(struct request_queue *q, struct request *rq),
	      TP_ARGS(q, rq));

DECLARE_TRACE(block_rq_insert,
	TP_PROTO(struct request_queue *q, struct request *rq),
	      TP_ARGS(q, rq));

DECLARE_TRACE(block_rq_issue,
	TP_PROTO(struct request_queue *q, struct request *rq),
	      TP_ARGS(q, rq));

DECLARE_TRACE(block_rq_requeue,
	TP_PROTO(struct request_queue *q, struct request *rq),
	      TP_ARGS(q, rq));

DECLARE_TRACE(block_rq_complete,
	TP_PROTO(struct request_queue *q, struct request *rq),
	      TP_ARGS(q, rq));

DECLARE_TRACE(block_bio_bounce,
	TP_PROTO(struct request_queue *q, struct bio *bio),
	      TP_ARGS(q, bio));

DECLARE_TRACE(block_bio_complete,
	TP_PROTO(struct request_queue *q, struct bio *bio),
	      TP_ARGS(q, bio));

DECLARE_TRACE(block_bio_backmerge,
	TP_PROTO(struct request_queue *q, struct bio *bio),
	      TP_ARGS(q, bio));

DECLARE_TRACE(block_bio_frontmerge,
	TP_PROTO(struct request_queue *q, struct bio *bio),
	      TP_ARGS(q, bio));

DECLARE_TRACE(block_bio_queue,
	TP_PROTO(struct request_queue *q, struct bio *bio),
	      TP_ARGS(q, bio));

DECLARE_TRACE(block_getrq,
	TP_PROTO(struct request_queue *q, struct bio *bio, int rw),
	      TP_ARGS(q, bio, rw));

DECLARE_TRACE(block_sleeprq,
	TP_PROTO(struct request_queue *q, struct bio *bio, int rw),
	      TP_ARGS(q, bio, rw));

DECLARE_TRACE(block_plug,
	TP_PROTO(struct request_queue *q),
	      TP_ARGS(q));

DECLARE_TRACE(block_unplug_timer,
	TP_PROTO(struct request_queue *q),
	      TP_ARGS(q));

DECLARE_TRACE(block_unplug_io,
	TP_PROTO(struct request_queue *q),
	      TP_ARGS(q));

DECLARE_TRACE(block_split,
	TP_PROTO(struct request_queue *q, struct bio *bio, unsigned int pdu),
	      TP_ARGS(q, bio, pdu));

DECLARE_TRACE(block_remap,
	TP_PROTO(struct request_queue *q, struct bio *bio, dev_t dev,
		 sector_t from, sector_t to),
	      TP_ARGS(q, bio, dev, from, to));

#endif
