/*	$NetBSD: unbzip2.c,v 1.14 2017/08/04 07:27:08 mrg Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 2006 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Simon Burge.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/* This file is #included by gzip.c */

static off_t
unbzip2(int in, int out, char *pre, size_t prelen, off_t *bytes_in)
{
	int		ret, end_of_file, cold = 0;
	off_t		bytes_out = 0;
	bz_stream	bzs;
	static char	*inbuf, *outbuf;

	if (inbuf == NULL)
		inbuf = malloc(BUFLEN);
	if (outbuf == NULL)
		outbuf = malloc(BUFLEN);
	if (inbuf == NULL || outbuf == NULL)
	        maybe_err("malloc");

	bzs.bzalloc = NULL;
	bzs.bzfree = NULL;
	bzs.opaque = NULL;

	end_of_file = 0;
	ret = BZ2_bzDecompressInit(&bzs, 0, 0);
	if (ret != BZ_OK)
	        maybe_errx("bzip2 init");

	/* Prepend. */
	bzs.avail_in = prelen;
	bzs.next_in = pre;

	if (bytes_in)
		*bytes_in = prelen;

	while (ret == BZ_OK) {
		check_siginfo();
	        if (bzs.avail_in == 0 && !end_of_file) {
			ssize_t	n;

	                n = read(in, inbuf, BUFLEN);
	                if (n < 0)
	                        maybe_err("read");
	                if (n == 0)
	                        end_of_file = 1;
			infile_newdata(n);
	                bzs.next_in = inbuf;
	                bzs.avail_in = n;
			if (bytes_in)
				*bytes_in += n;
	        }

	        bzs.next_out = outbuf;
	        bzs.avail_out = BUFLEN;
	        ret = BZ2_bzDecompress(&bzs);

	        switch (ret) {
	        case BZ_STREAM_END:
	        case BZ_OK:
	                if (ret == BZ_OK && end_of_file) {
				/*
				 * If we hit this after a stream end, consider
				 * it as the end of the whole file and don't
				 * bail out.
				 */
				if (cold == 1)
					ret = BZ_STREAM_END;
				else
					maybe_errx("truncated file");
			}
			cold = 0;
	                if (!tflag && bzs.avail_out != BUFLEN) {
				ssize_t	n;

	                        n = write(out, outbuf, BUFLEN - bzs.avail_out);
	                        if (n < 0)
	                                maybe_err("write");
	                	bytes_out += n;
	                }
			if (ret == BZ_STREAM_END && !end_of_file) {
				if (BZ2_bzDecompressEnd(&bzs) != BZ_OK ||
				    BZ2_bzDecompressInit(&bzs, 0, 0) != BZ_OK)
					maybe_errx("bzip2 re-init");
				cold = 1;
				ret = BZ_OK;
			}
			break;

	        case BZ_DATA_ERROR:
	                maybe_warnx("bzip2 data integrity error");
			break;

	        case BZ_DATA_ERROR_MAGIC:
	                maybe_warnx("bzip2 magic number error");
			break;

	        case BZ_MEM_ERROR:
	                maybe_warnx("bzip2 out of memory");
			break;
		
		default:	
			maybe_warnx("unknown bzip2 error: %d", ret);
			break;
	        }
	}

	if (ret != BZ_STREAM_END || BZ2_bzDecompressEnd(&bzs) != BZ_OK)
	        return (-1);

	return (bytes_out);
}

