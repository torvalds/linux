/*	$NetBSD: unxz.c,v 1.8 2018/10/06 16:36:45 martin Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Christos Zoulas.
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
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stdarg.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <lzma.h>

static off_t
unxz(int i, int o, char *pre, size_t prelen, off_t *bytes_in)
{
	lzma_stream strm = LZMA_STREAM_INIT;
	static const int flags = LZMA_TELL_UNSUPPORTED_CHECK|LZMA_CONCATENATED;
	lzma_ret ret;
	lzma_action action = LZMA_RUN;
	off_t bytes_out, bp;
	uint8_t ibuf[BUFSIZ];
	uint8_t obuf[BUFSIZ];

	if (bytes_in == NULL)
		bytes_in = &bp;

	strm.next_in = ibuf;
	memcpy(ibuf, pre, prelen);
	strm.avail_in = read(i, ibuf + prelen, sizeof(ibuf) - prelen);
	if (strm.avail_in == (size_t)-1)
		maybe_err("read failed");
	infile_newdata(strm.avail_in);
	strm.avail_in += prelen;
	*bytes_in = strm.avail_in;

	if ((ret = lzma_stream_decoder(&strm, UINT64_MAX, flags)) != LZMA_OK)
		maybe_errx("Can't initialize decoder (%d)", ret);

	strm.next_out = NULL;
	strm.avail_out = 0;
	if ((ret = lzma_code(&strm, LZMA_RUN)) != LZMA_OK)
		maybe_errx("Can't read headers (%d)", ret);

	bytes_out = 0;
	strm.next_out = obuf;
	strm.avail_out = sizeof(obuf);

	for (;;) {
		check_siginfo();
		if (strm.avail_in == 0) {
			strm.next_in = ibuf;
			strm.avail_in = read(i, ibuf, sizeof(ibuf));
			switch (strm.avail_in) {
			case (size_t)-1:
				maybe_err("read failed");
				/*NOTREACHED*/
			case 0:
				action = LZMA_FINISH;
				break;
			default:
				infile_newdata(strm.avail_in);
				*bytes_in += strm.avail_in;
				break;
			}
		}

		ret = lzma_code(&strm, action);

		// Write and check write error before checking decoder error.
		// This way as much data as possible gets written to output
		// even if decoder detected an error.
		if (strm.avail_out == 0 || ret != LZMA_OK) {
			const size_t write_size = sizeof(obuf) - strm.avail_out;

			if (write(o, obuf, write_size) != (ssize_t)write_size)
				maybe_err("write failed");

			strm.next_out = obuf;
			strm.avail_out = sizeof(obuf);
			bytes_out += write_size;
		}

		if (ret != LZMA_OK) {
			if (ret == LZMA_STREAM_END) {
				// Check that there's no trailing garbage.
				if (strm.avail_in != 0 || read(i, ibuf, 1))
					ret = LZMA_DATA_ERROR;
				else {
					lzma_end(&strm);
					return bytes_out;
				}
			}

			const char *msg;
			switch (ret) {
			case LZMA_MEM_ERROR:
				msg = strerror(ENOMEM);
				break;

			case LZMA_FORMAT_ERROR:
				msg = "File format not recognized";
				break;

			case LZMA_OPTIONS_ERROR:
				// FIXME: Better message?
				msg = "Unsupported compression options";
				break;

			case LZMA_DATA_ERROR:
				msg = "File is corrupt";
				break;

			case LZMA_BUF_ERROR:
				msg = "Unexpected end of input";
				break;

			case LZMA_MEMLIMIT_ERROR:
				msg = "Reached memory limit";
				break;

			default:
				maybe_errx("Unknown error (%d)", ret);
				break;
			}
			maybe_errx("%s", msg);

		}
	}
}

#include <stdbool.h>

/*
 * Copied various bits and pieces from xz support code or brute force
 * replacements.
 */

#define	my_min(A,B)	((A)<(B)?(A):(B))

// Some systems have suboptimal BUFSIZ. Use a bit bigger value on them.
// We also need that IO_BUFFER_SIZE is a multiple of 8 (sizeof(uint64_t))
#if BUFSIZ <= 1024
#       define IO_BUFFER_SIZE 8192
#else
#       define IO_BUFFER_SIZE (BUFSIZ & ~7U)
#endif

/// is_sparse() accesses the buffer as uint64_t for maximum speed.
/// Use an union to make sure that the buffer is properly aligned.
typedef union {
        uint8_t u8[IO_BUFFER_SIZE];
        uint32_t u32[IO_BUFFER_SIZE / sizeof(uint32_t)];
        uint64_t u64[IO_BUFFER_SIZE / sizeof(uint64_t)];
} io_buf;


static bool
io_pread(int fd, io_buf *buf, size_t size, off_t pos)
{
	// Using lseek() and read() is more portable than pread() and
	// for us it is as good as real pread().
	if (lseek(fd, pos, SEEK_SET) != pos) {
		return true;
	}

	const size_t amount = read(fd, buf, size);
	if (amount == SIZE_MAX)
		return true;

	if (amount != size) {
		return true;
	}

	return false;
}

/*
 * Most of the following is copied (mostly verbatim) from the xz
 * distribution, from file src/xz/list.c
 */

///////////////////////////////////////////////////////////////////////////////
//
/// \file       list.c
/// \brief      Listing information about .xz files
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////


/// Information about a .xz file
typedef struct {
	/// Combined Index of all Streams in the file
	lzma_index *idx;

	/// Total amount of Stream Padding
	uint64_t stream_padding;

	/// Highest memory usage so far
	uint64_t memusage_max;

	/// True if all Blocks so far have Compressed Size and
	/// Uncompressed Size fields
	bool all_have_sizes;

	/// Oldest XZ Utils version that will decompress the file
	uint32_t min_version;

} xz_file_info;

#define XZ_FILE_INFO_INIT { NULL, 0, 0, true, 50000002 }


/// \brief      Parse the Index(es) from the given .xz file
///
/// \param      xfi     Pointer to structure where the decoded information
///                     is stored.
/// \param      pair    Input file
///
/// \return     On success, false is returned. On error, true is returned.
///
// TODO: This function is pretty big. liblzma should have a function that
// takes a callback function to parse the Index(es) from a .xz file to make
// it easy for applications.
static bool
parse_indexes(xz_file_info *xfi, int src_fd)
{
	struct stat st;

	fstat(src_fd, &st);
	if (st.st_size <= 0) {
		return true;
	}

	if (st.st_size < 2 * LZMA_STREAM_HEADER_SIZE) {
		return true;
	}

	io_buf buf;
	lzma_stream_flags header_flags;
	lzma_stream_flags footer_flags;
	lzma_ret ret;

	// lzma_stream for the Index decoder
	lzma_stream strm = LZMA_STREAM_INIT;

	// All Indexes decoded so far
	lzma_index *combined_index = NULL;

	// The Index currently being decoded
	lzma_index *this_index = NULL;

	// Current position in the file. We parse the file backwards so
	// initialize it to point to the end of the file.
	off_t pos = st.st_size;

	// Each loop iteration decodes one Index.
	do {
		// Check that there is enough data left to contain at least
		// the Stream Header and Stream Footer. This check cannot
		// fail in the first pass of this loop.
		if (pos < 2 * LZMA_STREAM_HEADER_SIZE) {
			goto error;
		}

		pos -= LZMA_STREAM_HEADER_SIZE;
		lzma_vli stream_padding = 0;

		// Locate the Stream Footer. There may be Stream Padding which
		// we must skip when reading backwards.
		while (true) {
			if (pos < LZMA_STREAM_HEADER_SIZE) {
				goto error;
			}

			if (io_pread(src_fd, &buf,
					LZMA_STREAM_HEADER_SIZE, pos))
				goto error;

			// Stream Padding is always a multiple of four bytes.
			int i = 2;
			if (buf.u32[i] != 0)
				break;

			// To avoid calling io_pread() for every four bytes
			// of Stream Padding, take advantage that we read
			// 12 bytes (LZMA_STREAM_HEADER_SIZE) already and
			// check them too before calling io_pread() again.
			do {
				stream_padding += 4;
				pos -= 4;
				--i;
			} while (i >= 0 && buf.u32[i] == 0);
		}

		// Decode the Stream Footer.
		ret = lzma_stream_footer_decode(&footer_flags, buf.u8);
		if (ret != LZMA_OK) {
			goto error;
		}

		// Check that the Stream Footer doesn't specify something
		// that we don't support. This can only happen if the xz
		// version is older than liblzma and liblzma supports
		// something new.
		//
		// It is enough to check Stream Footer. Stream Header must
		// match when it is compared against Stream Footer with
		// lzma_stream_flags_compare().
		if (footer_flags.version != 0) {
			goto error;
		}

		// Check that the size of the Index field looks sane.
		lzma_vli index_size = footer_flags.backward_size;
		if ((lzma_vli)(pos) < index_size + LZMA_STREAM_HEADER_SIZE) {
			goto error;
		}

		// Set pos to the beginning of the Index.
		pos -= index_size;

		// Decode the Index.
		ret = lzma_index_decoder(&strm, &this_index, UINT64_MAX);
		if (ret != LZMA_OK) {
			goto error;
		}

		do {
			// Don't give the decoder more input than the
			// Index size.
			strm.avail_in = my_min(IO_BUFFER_SIZE, index_size);
			if (io_pread(src_fd, &buf, strm.avail_in, pos))
				goto error;

			pos += strm.avail_in;
			index_size -= strm.avail_in;

			strm.next_in = buf.u8;
			ret = lzma_code(&strm, LZMA_RUN);

		} while (ret == LZMA_OK);

		// If the decoding seems to be successful, check also that
		// the Index decoder consumed as much input as indicated
		// by the Backward Size field.
		if (ret == LZMA_STREAM_END)
			if (index_size != 0 || strm.avail_in != 0)
				ret = LZMA_DATA_ERROR;

		if (ret != LZMA_STREAM_END) {
			// LZMA_BUFFER_ERROR means that the Index decoder
			// would have liked more input than what the Index
			// size should be according to Stream Footer.
			// The message for LZMA_DATA_ERROR makes more
			// sense in that case.
			if (ret == LZMA_BUF_ERROR)
				ret = LZMA_DATA_ERROR;

			goto error;
		}

		// Decode the Stream Header and check that its Stream Flags
		// match the Stream Footer.
		pos -= footer_flags.backward_size + LZMA_STREAM_HEADER_SIZE;
		if ((lzma_vli)(pos) < lzma_index_total_size(this_index)) {
			goto error;
		}

		pos -= lzma_index_total_size(this_index);
		if (io_pread(src_fd, &buf, LZMA_STREAM_HEADER_SIZE, pos))
			goto error;

		ret = lzma_stream_header_decode(&header_flags, buf.u8);
		if (ret != LZMA_OK) {
			goto error;
		}

		ret = lzma_stream_flags_compare(&header_flags, &footer_flags);
		if (ret != LZMA_OK) {
			goto error;
		}

		// Store the decoded Stream Flags into this_index. This is
		// needed so that we can print which Check is used in each
		// Stream.
		ret = lzma_index_stream_flags(this_index, &footer_flags);
		if (ret != LZMA_OK)
			goto error;

		// Store also the size of the Stream Padding field. It is
		// needed to show the offsets of the Streams correctly.
		ret = lzma_index_stream_padding(this_index, stream_padding);
		if (ret != LZMA_OK)
			goto error;

		if (combined_index != NULL) {
			// Append the earlier decoded Indexes
			// after this_index.
			ret = lzma_index_cat(
					this_index, combined_index, NULL);
			if (ret != LZMA_OK) {
				goto error;
			}
		}

		combined_index = this_index;
		this_index = NULL;

		xfi->stream_padding += stream_padding;

	} while (pos > 0);

	lzma_end(&strm);

	// All OK. Make combined_index available to the caller.
	xfi->idx = combined_index;
	return false;

error:
	// Something went wrong, free the allocated memory.
	lzma_end(&strm);
	lzma_index_end(combined_index, NULL);
	lzma_index_end(this_index, NULL);
	return true;
}

/***************** end of copy form list.c *************************/

/*
 * Small wrapper to extract total length of a file
 */
off_t
unxz_len(int fd)
{
	xz_file_info xfi = XZ_FILE_INFO_INIT;
	if (!parse_indexes(&xfi, fd)) {
		off_t res = lzma_index_uncompressed_size(xfi.idx);
		lzma_index_end(xfi.idx, NULL);
		return res;
	}
	return 0;
}

