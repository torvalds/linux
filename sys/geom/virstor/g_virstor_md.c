/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 Ivan Voras <ivoras@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/endian.h>

#include <geom/virstor/g_virstor_md.h>
#include <geom/virstor/binstream.h>

/*
 * Encode data from g_virstor_metadata structure into a endian-independant
 * byte stream.
 */
void
virstor_metadata_encode(struct g_virstor_metadata *md, unsigned char *data)
{
	bin_stream_t bs;

	bs_open(&bs, data);

	bs_write_buf(&bs, md->md_magic, sizeof(md->md_magic));
	bs_write_u32(&bs, md->md_version);
	bs_write_buf(&bs, md->md_name, sizeof(md->md_name));
	bs_write_u64(&bs, md->md_virsize);
	bs_write_u32(&bs, md->md_chunk_size);
	bs_write_u32(&bs, md->md_id);
	bs_write_u16(&bs, md->md_count);

	bs_write_buf(&bs, md->provider, sizeof(md->provider));
	bs_write_u16(&bs, md->no);
	bs_write_u64(&bs, md->provsize);
	bs_write_u32(&bs, md->chunk_count);
	bs_write_u32(&bs, md->chunk_next);
	bs_write_u16(&bs, md->chunk_reserved);
	bs_write_u16(&bs, md->flags);
}


/*
 * Decode data from endian-independant byte stream into g_virstor_metadata
 * structure.
 */
void
virstor_metadata_decode(unsigned char *data, struct g_virstor_metadata *md)
{
	bin_stream_t bs;

	bs_open(&bs, (char *)(data));

	bs_read_buf(&bs, md->md_magic, sizeof(md->md_magic));
	md->md_version = bs_read_u32(&bs);
	bs_read_buf(&bs, md->md_name, sizeof(md->md_name));
	md->md_virsize = bs_read_u64(&bs);
	md->md_chunk_size = bs_read_u32(&bs);
	md->md_id = bs_read_u32(&bs);
	md->md_count = bs_read_u16(&bs);

	bs_read_buf(&bs, md->provider, sizeof(md->provider));
	md->no = bs_read_u16(&bs);
	md->provsize = bs_read_u64(&bs);
	md->chunk_count = bs_read_u32(&bs);
	md->chunk_next = bs_read_u32(&bs);
	md->chunk_reserved = bs_read_u16(&bs);
	md->flags = bs_read_u16(&bs);
}
