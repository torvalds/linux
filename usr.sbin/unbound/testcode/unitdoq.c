/*
 * testcode/unitdoq.c - unit test for doq routines.
 *
 * Copyright (c) 2022, NLnet Labs. All rights reserved.
 *
 * This software is open source.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

/**
 * \file
 * Calls doq related unit tests. Exits with code 1 on a failure.
 */

#include "config.h"

#ifdef HAVE_NGTCP2

#include "util/netevent.h"
#include "services/listen_dnsport.h"
#include "testcode/unitmain.h"

/** check the size of a connection for doq */
static void
doq_size_conn_check()
{
	/* Printout the size of one doq connection, in memory usage.
	 * A connection with a couple cids, of type doq_conid, and
	 * it has one stream, and that has a query and an answer. */
	size_t answer_size = 233; /* size of www.nlnetlabs.nl minimal answer
		with dnssec and one A record. The unsigned answer is 176 with
		additional data, 61 bytes minimal response one A record. */
	size_t query_size = 45; /* size of query for www.nlnetlabs.nl, with
		an EDNS record with DO flag. */
	size_t conn_size = sizeof(struct doq_conn);
	size_t conid_size = sizeof(struct doq_conid);
	size_t stream_size = sizeof(struct doq_stream);

	conn_size += 16; /* DCID len in the conn key */
	conn_size += 0; /* the size of the ngtcp2_conn */
	conn_size += 0; /* the size of the SSL record */
	conn_size += 0; /* size of the close pkt,
		but we do not count it here. Only if the conn gets closed. */
	conid_size += 16; /* the dcid of the conn key */
	conid_size += 16; /* the cid */
	stream_size += query_size; /* size of in buffer */
	stream_size += answer_size; /* size of out buffer */
	printf("doq connection size %u bytes\n", (unsigned)(conn_size +
		conid_size*3 + stream_size));
}

void doq_test(void)
{
	unit_show_feature("doq");
	doq_size_conn_check();
}
#endif /* HAVE_NGTCP2 */
