/*-
 * Copyright (c) 2004 Max Khon
 * Copyright (c) 2014 Juniper Networks, Inc.
 * Copyright (c) 2006-2016 Maxim Sobolev <sobomax@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

struct g_uzip_softc;
struct bio;

DEFINE_RAW_METHOD(g_uzip_do, void, struct g_uzip_softc *, struct bio *);

struct g_uzip_softc {
	uint32_t blksz;                 /* block size */
	uint32_t nblocks;               /* number of blocks */
	struct g_uzip_blk *toc;         /* table of contents */

	struct mtx last_mtx;
	uint32_t last_blk;              /* last blk no */
	char *last_buf;                 /* last blk data */
	int req_total;                  /* total requests */
	int req_cached;                 /* cached requests */
	struct g_uzip_dapi *dcp;        /* decompressor instance */

	g_uzip_do_t uzip_do;

	struct proc *procp;
	struct bio_queue_head bio_queue; 
	struct mtx queue_mtx;
	unsigned wrkthr_flags;
#define	GUZ_SHUTDOWN	(0x1 << 0)
#define	GUZ_EXITING	(0x1 << 1)
};
