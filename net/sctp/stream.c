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
