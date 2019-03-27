/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2016 Jakub Klama <jceel@FreeBSD.org>.
 * Copyright (c) 2018 Alexander Motin <mav@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/types.h>
#include <sys/uio.h>

#include <stdlib.h>
#include <string.h>
#include "iov.h"

void
seek_iov(const struct iovec *iov1, int niov1, struct iovec *iov2, int *niov2,
    size_t seek)
{
	size_t remainder = 0;
	size_t left = seek;
	int i, j;

	for (i = 0; i < niov1; i++) {
		size_t toseek = MIN(left, iov1[i].iov_len);
		left -= toseek;

		if (toseek == iov1[i].iov_len)
			continue;

		if (left == 0) {
			remainder = toseek;
			break;
		}
	}

	for (j = i; j < niov1; j++) {
		iov2[j - i].iov_base = (char *)iov1[j].iov_base + remainder;
		iov2[j - i].iov_len = iov1[j].iov_len - remainder;
		remainder = 0;
	}

	*niov2 = j - i;
}

size_t
count_iov(const struct iovec *iov, int niov)
{
	size_t total = 0;
	int i;

	for (i = 0; i < niov; i++)
		total += iov[i].iov_len;

	return (total);
}

void
truncate_iov(struct iovec *iov, int *niov, size_t length)
{
	size_t done = 0;
	int i;

	for (i = 0; i < *niov; i++) {
		size_t toseek = MIN(length - done, iov[i].iov_len);
		done += toseek;

		if (toseek <= iov[i].iov_len) {
			iov[i].iov_len = toseek;
			*niov = i + 1;
			return;
		}
	}
}

ssize_t
iov_to_buf(const struct iovec *iov, int niov, void **buf)
{
	size_t ptr, total;
	int i;

	total = count_iov(iov, niov);
	*buf = realloc(*buf, total);
	if (*buf == NULL)
		return (-1);

	for (i = 0, ptr = 0; i < niov; i++) {
		memcpy(*buf + ptr, iov[i].iov_base, iov[i].iov_len);
		ptr += iov[i].iov_len;
	}

	return (total);
}

ssize_t
buf_to_iov(const void *buf, size_t buflen, struct iovec *iov, int niov,
    size_t seek)
{
	struct iovec *diov;
	int ndiov, i;
	size_t off = 0, len;

	if (seek > 0) {
		diov = malloc(sizeof(struct iovec) * niov);
		seek_iov(iov, niov, diov, &ndiov, seek);
	} else {
		diov = iov;
		ndiov = niov;
	}

	for (i = 0; i < ndiov && off < buflen; i++) {
		len = MIN(diov[i].iov_len, buflen - off);
		memcpy(diov[i].iov_base, buf + off, len);
		off += len;
	}

	if (seek > 0)
		free(diov);

	return ((ssize_t)off);
}

