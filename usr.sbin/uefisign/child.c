/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2014 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Edward Tomasz Napierala under sponsorship
 * from the FreeBSD Foundation.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/capsicum.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>
#include <capsicum_helpers.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/pem.h>

#include "uefisign.h"

static void
load(struct executable *x)
{
	int error, fd;
	struct stat sb;
	char *buf;
	size_t nread, len;

	fd = fileno(x->x_fp);

	error = fstat(fd, &sb);
	if (error != 0)
		err(1, "%s: fstat", x->x_path);

	len = sb.st_size;
	if (len <= 0)
		errx(1, "%s: file is empty", x->x_path);

	buf = malloc(len);
	if (buf == NULL)
		err(1, "%s: cannot malloc %zd bytes", x->x_path, len);

	nread = fread(buf, len, 1, x->x_fp);
	if (nread != 1)
		err(1, "%s: fread", x->x_path);

	x->x_buf = buf;
	x->x_len = len;
}

static void
digest_range(struct executable *x, EVP_MD_CTX *mdctx, off_t off, size_t len)
{
	int ok;

	range_check(x, off, len, "chunk");

	ok = EVP_DigestUpdate(mdctx, x->x_buf + off, len);
	if (ok == 0) {
		ERR_print_errors_fp(stderr);
		errx(1, "EVP_DigestUpdate(3) failed");
	}
}

static void
digest(struct executable *x)
{
	EVP_MD_CTX *mdctx;
	const EVP_MD *md;
	size_t sum_of_bytes_hashed;
	int i, ok;

	/*
	 * Windows Authenticode Portable Executable Signature Format
	 * spec version 1.0 specifies MD5 and SHA1.  However, pesign
	 * and sbsign both use SHA256, so do the same.
	 */
	md = EVP_get_digestbyname(DIGEST);
	if (md == NULL) {
		ERR_print_errors_fp(stderr);
		errx(1, "EVP_get_digestbyname(\"%s\") failed", DIGEST);
	}

	mdctx = EVP_MD_CTX_create();
	if (mdctx == NULL) {
		ERR_print_errors_fp(stderr);
		errx(1, "EVP_MD_CTX_create(3) failed");
	}

	ok = EVP_DigestInit_ex(mdctx, md, NULL);
	if (ok == 0) {
		ERR_print_errors_fp(stderr);
		errx(1, "EVP_DigestInit_ex(3) failed");
	}

	/*
	 * According to the Authenticode spec, we need to compute
	 * the digest in a rather... specific manner; see "Calculating
	 * the PE Image Hash" part of the spec for details.
	 *
	 * First, everything from 0 to before the PE checksum.
	 */
	digest_range(x, mdctx, 0, x->x_checksum_off);

	/*
	 * Second, from after the PE checksum to before the Certificate
	 * entry in Data Directory.
	 */
	digest_range(x, mdctx, x->x_checksum_off + x->x_checksum_len,
	    x->x_certificate_entry_off -
	    (x->x_checksum_off + x->x_checksum_len));

	/*
	 * Then, from after the Certificate entry to the end of headers.
	 */
	digest_range(x, mdctx,
	    x->x_certificate_entry_off + x->x_certificate_entry_len,
	    x->x_headers_len -
	    (x->x_certificate_entry_off + x->x_certificate_entry_len));

	/*
	 * Then, each section in turn, as specified in the PE Section Table.
	 *
	 * XXX: Sorting.
	 */
	sum_of_bytes_hashed = x->x_headers_len;
	for (i = 0; i < x->x_nsections; i++) {
		digest_range(x, mdctx,
		    x->x_section_off[i], x->x_section_len[i]);
		sum_of_bytes_hashed += x->x_section_len[i];
	}

	/*
	 * I believe this can happen with overlapping sections.
	 */
	if (sum_of_bytes_hashed > x->x_len)
		errx(1, "number of bytes hashed is larger than file size");

	/*
	 * I can't really explain this one; just do what the spec says.
	 */
	if (sum_of_bytes_hashed < x->x_len) {
		digest_range(x, mdctx, sum_of_bytes_hashed,
		    x->x_len - (signature_size(x) + sum_of_bytes_hashed));
	}

	ok = EVP_DigestFinal_ex(mdctx, x->x_digest, &x->x_digest_len);
	if (ok == 0) {
		ERR_print_errors_fp(stderr);
		errx(1, "EVP_DigestFinal_ex(3) failed");
	}

	EVP_MD_CTX_destroy(mdctx);
}

static void
show_digest(const struct executable *x)
{
	int i;

	printf("computed %s digest ", DIGEST);
	for (i = 0; i < (int)x->x_digest_len; i++)
		printf("%02x", (unsigned char)x->x_digest[i]);
	printf("; digest len %u\n", x->x_digest_len);
}

static void
send_digest(const struct executable *x, int pipefd)
{

	send_chunk(x->x_digest, x->x_digest_len, pipefd);
}

static void
receive_signature(struct executable *x, int pipefd)
{

	receive_chunk(&x->x_signature, &x->x_signature_len, pipefd);
}

static void
save(struct executable *x, FILE *fp, const char *path)
{
	size_t nwritten;

	assert(fp != NULL);
	assert(path != NULL);

	nwritten = fwrite(x->x_buf, x->x_len, 1, fp);
	if (nwritten != 1)
		err(1, "%s: fwrite", path);
}

int
child(const char *inpath, const char *outpath, int pipefd,
    bool Vflag, bool vflag)
{
	FILE *outfp = NULL, *infp = NULL;
	struct executable *x;

	infp = checked_fopen(inpath, "r");
	if (outpath != NULL)
		outfp = checked_fopen(outpath, "w");

	if (caph_enter() < 0)
		err(1, "cap_enter");

	x = calloc(1, sizeof(*x));
	if (x == NULL)
		err(1, "calloc");
	x->x_path = inpath;
	x->x_fp = infp;

	load(x);
	parse(x);
	if (Vflag) {
		if (signature_size(x) == 0)
			errx(1, "file not signed");

		printf("file contains signature\n");
		if (vflag) {
			digest(x);
			show_digest(x);
			show_certificate(x);
		}
	} else {
		if (signature_size(x) != 0)
			errx(1, "file already signed");

		digest(x);
		if (vflag)
			show_digest(x);
		send_digest(x, pipefd);
		receive_signature(x, pipefd);
		update(x);
		save(x, outfp, outpath);
	}

	return (0);
}
