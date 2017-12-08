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
 *   Xin Long <lucien.xin@gmail.com>
 */

#ifndef __sctp_stream_interleave_h__
#define __sctp_stream_interleave_h__

struct sctp_stream_interleave {
	__u16	data_chunk_len;
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
};

void sctp_stream_interleave_init(struct sctp_stream *stream);

#endif /* __sctp_stream_interleave_h__ */
