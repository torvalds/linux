#ifndef _TRACE_BLOCK_H
#define _TRACE_BLOCK_H

#include <linux/blkdev.h>
#include <linux/tracepoint.h>

DEFINE_TRACE(block_rq_abort,
	TPPROTO(struct request_queue *q, struct request *rq),
	TPARGS(q, rq));
DEFINE_TRACE(block_rq_insert,
	TPPROTO(struct request_queue *q, struct request *rq),
	TPARGS(q, rq));
DEFINE_TRACE(block_rq_issue,
	TPPROTO(struct request_queue *q, struct request *rq),
	TPARGS(q, rq));
DEFINE_TRACE(block_rq_requeue,
	TPPROTO(struct request_queue *q, struct request *rq),
	TPARGS(q, rq));
DEFINE_TRACE(block_rq_complete,
	TPPROTO(struct request_queue *q, struct request *rq),
	TPARGS(q, rq));
DEFINE_TRACE(block_bio_bounce,
	TPPROTO(struct request_queue *q, struct bio *bio),
	TPARGS(q, bio));
DEFINE_TRACE(block_bio_complete,
	TPPROTO(struct request_queue *q, struct bio *bio),
	TPARGS(q, bio));
DEFINE_TRACE(block_bio_backmerge,
	TPPROTO(struct request_queue *q, struct bio *bio),
	TPARGS(q, bio));
DEFINE_TRACE(block_bio_frontmerge,
	TPPROTO(struct request_queue *q, struct bio *bio),
	TPARGS(q, bio));
DEFINE_TRACE(block_bio_queue,
	TPPROTO(struct request_queue *q, struct bio *bio),
	TPARGS(q, bio));
DEFINE_TRACE(block_getrq,
	TPPROTO(struct request_queue *q, struct bio *bio, int rw),
	TPARGS(q, bio, rw));
DEFINE_TRACE(block_sleeprq,
	TPPROTO(struct request_queue *q, struct bio *bio, int rw),
	TPARGS(q, bio, rw));
DEFINE_TRACE(block_plug,
	TPPROTO(struct request_queue *q),
	TPARGS(q));
DEFINE_TRACE(block_unplug_timer,
	TPPROTO(struct request_queue *q),
	TPARGS(q));
DEFINE_TRACE(block_unplug_io,
	TPPROTO(struct request_queue *q),
	TPARGS(q));
DEFINE_TRACE(block_split,
	TPPROTO(struct request_queue *q, struct bio *bio, unsigned int pdu),
	TPARGS(q, bio, pdu));
DEFINE_TRACE(block_remap,
	TPPROTO(struct request_queue *q, struct bio *bio, dev_t dev,
		sector_t from, sector_t to),
	TPARGS(q, bio, dev, from, to));

#endif
