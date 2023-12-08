/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SCTP kernel implementation
 * (C) Copyright Red Hat Inc. 2017
 *
 * These are definitions used by the stream schedulers, defined in RFC
 * draft ndata (https://tools.ietf.org/html/draft-ietf-tsvwg-sctp-ndata-11)
 *
 * Please send any bug reports or fixes you make to the
 * email addresses:
 *    lksctp developers <linux-sctp@vger.kernel.org>
 *
 * Written or modified by:
 *   Xin Long <lucien.xin@gmail.com>
 */

#ifndef __sctp_stream_interleave_h__
#define __sctp_stream_interleave_h__

struct sctp_stream_interleave {
	__u16	data_chunk_len;
	__u16	ftsn_chunk_len;
	/* (I-)DATA process */
	struct sctp_chunk *(*make_datafrag)(const struct sctp_association *asoc,
					    const struct sctp_sndrcvinfo *sinfo,
					    int len, __u8 flags, gfp_t gfp);
	void	(*assign_number)(struct sctp_chunk *chunk);
	bool	(*validate_data)(struct sctp_chunk *chunk);
	int	(*ulpevent_data)(struct sctp_ulpq *ulpq,
				 struct sctp_chunk *chunk, gfp_t gfp);
	int	(*enqueue_event)(struct sctp_ulpq *ulpq,
				 struct sctp_ulpevent *event);
	void	(*renege_events)(struct sctp_ulpq *ulpq,
				 struct sctp_chunk *chunk, gfp_t gfp);
	void	(*start_pd)(struct sctp_ulpq *ulpq, gfp_t gfp);
	void	(*abort_pd)(struct sctp_ulpq *ulpq, gfp_t gfp);
	/* (I-)FORWARD-TSN process */
	void	(*generate_ftsn)(struct sctp_outq *q, __u32 ctsn);
	bool	(*validate_ftsn)(struct sctp_chunk *chunk);
	void	(*report_ftsn)(struct sctp_ulpq *ulpq, __u32 ftsn);
	void	(*handle_ftsn)(struct sctp_ulpq *ulpq,
			       struct sctp_chunk *chunk);
};

void sctp_stream_interleave_init(struct sctp_stream *stream);

#endif /* __sctp_stream_interleave_h__ */
