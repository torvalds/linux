/* SCTP kernel implementation
 * (C) Copyright IBM Corp. 2001, 2004
 * Copyright (c) 1999-2000 Cisco, Inc.
 * Copyright (c) 1999-2001 Motorola, Inc.
 * Copyright (c) 2001 Intel Corp.
 *
 * This file is part of the SCTP kernel implementation
 *
 * These functions manipulate sctp tsn mapping array.
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
 * email address(es):
 *    lksctp developers <linux-sctp@vger.kernel.org>
 *
 * Written or modified by:
 *    Xin Long <lucien.xin@gmail.com>
 */

#include <net/sctp/sctp.h>
#include <net/sctp/sm.h>

struct sctp_stream *sctp_stream_new(__u16 incnt, __u16 outcnt, gfp_t gfp)
{
	struct sctp_stream *stream;
	int i;

	stream = kzalloc(sizeof(*stream), gfp);
	if (!stream)
		return NULL;

	stream->outcnt = outcnt;
	stream->out = kcalloc(stream->outcnt, sizeof(*stream->out), gfp);
	if (!stream->out) {
		kfree(stream);
		return NULL;
	}
	for (i = 0; i < stream->outcnt; i++)
		stream->out[i].state = SCTP_STREAM_OPEN;

	stream->incnt = incnt;
	stream->in = kcalloc(stream->incnt, sizeof(*stream->in), gfp);
	if (!stream->in) {
		kfree(stream->out);
		kfree(stream);
		return NULL;
	}

	return stream;
}

void sctp_stream_free(struct sctp_stream *stream)
{
	if (unlikely(!stream))
		return;

	kfree(stream->out);
	kfree(stream->in);
	kfree(stream);
}

void sctp_stream_clear(struct sctp_stream *stream)
{
	int i;

	for (i = 0; i < stream->outcnt; i++)
		stream->out[i].ssn = 0;

	for (i = 0; i < stream->incnt; i++)
		stream->in[i].ssn = 0;
}

static int sctp_send_reconf(struct sctp_association *asoc,
			    struct sctp_chunk *chunk)
{
	struct net *net = sock_net(asoc->base.sk);
	int retval = 0;

	retval = sctp_primitive_RECONF(net, asoc, chunk);
	if (retval)
		sctp_chunk_free(chunk);

	return retval;
}

int sctp_send_reset_streams(struct sctp_association *asoc,
			    struct sctp_reset_streams *params)
{
	struct sctp_stream *stream = asoc->stream;
	__u16 i, str_nums, *str_list;
	struct sctp_chunk *chunk;
	int retval = -EINVAL;
	bool out, in;

	if (!asoc->peer.reconf_capable ||
	    !(asoc->strreset_enable & SCTP_ENABLE_RESET_STREAM_REQ)) {
		retval = -ENOPROTOOPT;
		goto out;
	}

	if (asoc->strreset_outstanding) {
		retval = -EINPROGRESS;
		goto out;
	}

	out = params->srs_flags & SCTP_STREAM_RESET_OUTGOING;
	in  = params->srs_flags & SCTP_STREAM_RESET_INCOMING;
	if (!out && !in)
		goto out;

	str_nums = params->srs_number_streams;
	str_list = params->srs_stream_list;
	if (out && str_nums)
		for (i = 0; i < str_nums; i++)
			if (str_list[i] >= stream->outcnt)
				goto out;

	if (in && str_nums)
		for (i = 0; i < str_nums; i++)
			if (str_list[i] >= stream->incnt)
				goto out;

	chunk = sctp_make_strreset_req(asoc, str_nums, str_list, out, in);
	if (!chunk)
		goto out;

	if (out) {
		if (str_nums)
			for (i = 0; i < str_nums; i++)
				stream->out[str_list[i]].state =
						       SCTP_STREAM_CLOSED;
		else
			for (i = 0; i < stream->outcnt; i++)
				stream->out[i].state = SCTP_STREAM_CLOSED;
	}

	asoc->strreset_outstanding = out + in;
	asoc->strreset_chunk = chunk;
	sctp_chunk_hold(asoc->strreset_chunk);

	retval = sctp_send_reconf(asoc, chunk);
	if (retval) {
		sctp_chunk_put(asoc->strreset_chunk);
		asoc->strreset_chunk = NULL;
	}

out:
	return retval;
}
