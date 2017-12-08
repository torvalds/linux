/* SCTP kernel implementation
 * (C) Copyright Red Hat Inc. 2017
 *
 * This file is part of the SCTP kernel implementation
 *
 * These functions manipulate sctp stream queue/scheduling.
 *
 * This SCTP implementation is free software;
 * you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This SCTP implementation is distributed in the hope that it
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
 * email addresched(es):
 *    lksctp developers <linux-sctp@vger.kernel.org>
 *
 * Written or modified by:
 *    Xin Long <lucien.xin@gmail.com>
 */

#include <net/sctp/sctp.h>
#include <net/sctp/sm.h>
#include <linux/sctp.h>

static struct sctp_chunk *sctp_make_idatafrag_empty(
					const struct sctp_association *asoc,
					const struct sctp_sndrcvinfo *sinfo,
					int len, __u8 flags, gfp_t gfp)
{
	struct sctp_chunk *retval;
	struct sctp_idatahdr dp;

	memset(&dp, 0, sizeof(dp));
	dp.stream = htons(sinfo->sinfo_stream);

	if (sinfo->sinfo_flags & SCTP_UNORDERED)
		flags |= SCTP_DATA_UNORDERED;

	retval = sctp_make_idata(asoc, flags, sizeof(dp) + len, gfp);
	if (!retval)
		return NULL;

	retval->subh.idata_hdr = sctp_addto_chunk(retval, sizeof(dp), &dp);
	memcpy(&retval->sinfo, sinfo, sizeof(struct sctp_sndrcvinfo));

	return retval;
}

static struct sctp_stream_interleave sctp_stream_interleave_0 = {
	.data_chunk_len		= sizeof(struct sctp_data_chunk),
	/* DATA process functions */
	.make_datafrag		= sctp_make_datafrag_empty,
};

static struct sctp_stream_interleave sctp_stream_interleave_1 = {
	.data_chunk_len		= sizeof(struct sctp_idata_chunk),
	/* I-DATA process functions */
	.make_datafrag		= sctp_make_idatafrag_empty,
};

void sctp_stream_interleave_init(struct sctp_stream *stream)
{
	struct sctp_association *asoc;

	asoc = container_of(stream, struct sctp_association, stream);
	stream->si = asoc->intl_enable ? &sctp_stream_interleave_1
				       : &sctp_stream_interleave_0;
}
