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
 * $FreeBSD$
 */

#ifndef EFISIGN_H
#define	EFISIGN_H

#include <stdbool.h>
#include <openssl/evp.h>

#define	DIGEST		"SHA256"
#define	MAX_SECTIONS	128

struct executable {
	const char	*x_path;
	FILE		*x_fp;

	char		*x_buf;
	size_t		x_len;

	/*
	 * Set by pe_parse(), used by digest().
	 */
	size_t		x_headers_len;

	off_t		x_checksum_off;
	size_t		x_checksum_len;

	off_t		x_certificate_entry_off;
	size_t		x_certificate_entry_len;

	int		x_nsections;
	off_t		x_section_off[MAX_SECTIONS];
	size_t		x_section_len[MAX_SECTIONS];

	/*
	 * Computed by digest().
	 */
	unsigned char	x_digest[EVP_MAX_MD_SIZE];
	unsigned int	x_digest_len;

	/*
	 * Received from the parent process, which computes it in sign().
	 */
	void		*x_signature;
	size_t		x_signature_len;
};


FILE	*checked_fopen(const char *path, const char *mode);
void	send_chunk(const void *buf, size_t len, int pipefd);
void	receive_chunk(void **bufp, size_t *lenp, int pipefd);

int	child(const char *inpath, const char *outpath, int pipefd,
	    bool Vflag, bool vflag);

void	parse(struct executable *x);
void	update(struct executable *x);
size_t	signature_size(const struct executable *x);
void	show_certificate(const struct executable *x);
void	range_check(const struct executable *x,
	    off_t off, size_t len, const char *name);

#endif /* !EFISIGN_H */
