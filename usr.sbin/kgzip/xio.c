/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Global Technology Associates, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <err.h>
#include <string.h>
#include <unistd.h>

#include "kgzip.h"

/*
 * Close a file.
 */
void
xclose(const struct iodesc *id)
{
    if (close(id->fd))
	err(1, "%s", id->fname);
}

/*
 * Copy bytes from one file to another.
 */
void
xcopy(const struct iodesc * idi, const struct iodesc * ido,
      size_t nbyte, off_t offset)
{
    char buf[8192];
    size_t n;

    while (nbyte) {
	if ((n = sizeof(buf)) > nbyte)
	    n = nbyte;
	if (xread(idi, buf, n, offset) != n)
	    errx(1, "%s: Short read", idi->fname);
	xwrite(ido, buf, n);
	nbyte -= n;
	offset = -1;
    }
}

/*
 * Write binary zeroes to a file.
 */
void
xzero(const struct iodesc * id, size_t nbyte)
{
    char buf[8192];
    size_t n;

    memset(buf, 0, sizeof(buf));
    while (nbyte) {
	if ((n = sizeof(buf)) > nbyte)
	    n = nbyte;
	xwrite(id, buf, n);
	nbyte -= n;
    }
}

/*
 * Read from a file.
 */
size_t
xread(const struct iodesc * id, void *buf, size_t nbyte, off_t offset)
{
    ssize_t n;

    if (offset != -1 && lseek(id->fd, offset, SEEK_SET) != offset)
	err(1, "%s", id->fname);
    if ((n = read(id->fd, buf, nbyte)) == -1)
	err(1, "%s", id->fname);
    return (size_t)n;
}

/*
 * Write to a file.
 */
void
xwrite(const struct iodesc * id, const void *buf, size_t nbyte)
{
    ssize_t n;

    if ((n = write(id->fd, buf, nbyte)) == -1)
	err(1, "%s", id->fname);
    if ((size_t)n != nbyte)
	errx(1, "%s: Short write", id->fname);
}

/*
 * Reposition within a file.
 */
void
xseek(const struct iodesc *id, off_t offset)
{
    if (lseek(id->fd, offset, SEEK_SET) != offset)
	err(1, "%s", id->fname);
}
