/* SCTP kernel implementation
 * (C) Copyright Red Hat Inc. 2017
 *
 * These are definitions used by the stream schedulers, defined in RFC
 * draft ndata (https://tools.ietf.org/html/draft-ietf-tsvwg-sctp-ndata-11)
 *
 * This SCTP implementation is free software;
 * you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This SCTP implementation  is distributed in the hope that it
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 *                 ************************
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU CC; see the file COPYING.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * Please send any bug reports or fixes you make to the
 * email addresses:
 *    lksctp developers <linux-sctp@vger.kernel.org>
 *
 * Written or modified by:
 *   Marcelo Ricardo Leitner <marcelo.leitner@gmail.com>
 */

#ifndef __sctp_stream_sched_h__
#define __sctp_stream_sched_h__

struct sctp_sched_ops {
	/* Property handling for a given stream */
	int (*set)(struct sctp_stream *stream, __u16 sid, __u16 value,
		   gfp_t gfp);
	int (*get)(struct sctp_stream *stream, __u16 sid, __u16 *value);

	/* Init the specific scheduler */
	int (*init)(struct sctp_stream *stream);
	/* Init a stream */
	int (*init_sid)(struct sctp_stream *stream, __u16 sid, gfp_t gfp);
	/* Frees the entire thing */
	void (*free)(struct sctp_stream *stream);

	/* Enqueue a chunk */
	void (*enqueue)(struct sctp_outq *q, struct sctp_datamsg *msg);
	/* Dequeue a chunk */
	struct sctp_chunk *(*dequeue)(struct sctp_outq *q);
	/* Called only if the chunk fit the packet */
	void (*dequeue_done)(struct sctp_outq *q, struct sctp_chunk *chunk);
	/* Sched all chunks already enqueued */
	void (*sched_all)(struct sctp_stream *steam);
	/* Unched all chunks already enqueued */
	void (*unsched_all)(struct sctp_stream *steam);
};

int sctp_sched_set_sched(struct sctp_association *asoc,
			 enum sctp_sched_type sched);
int sctp_sched_get_sched(struct sctp_association *asoc);
int sctp_sched_set_value(struct sctp_association *asoc, __u16 sid,
			 __u16 value, gfp_t gfp);
int sctp_sched_get_value(struct sctp_association *asoc, __u16 sid,
			 __u16 *value);
void sctp_sched_dequeue_done(struct sctp_outq *q, struct sctp_chunk *ch);

void sctp_sched_dequeue_common(struct sctp_outq *q, struct sctp_chunk *ch);
int sctp_sched_init_sid(struct sctp_stream *stream, __u16 sid, gfp_t gfp);
struct sctp_sched_ops *sctp_sched_ops_from_stream(struct sctp_stream *stream);

#endif /* __sctp_stream_sched_h__ */
