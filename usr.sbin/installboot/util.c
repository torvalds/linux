/*	$OpenBSD: util.c,v 1.16 2022/02/03 10:25:14 visa Exp $	*/

/*
 * Copyright (c) 2014 Joel Sing <jsing@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/stat.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <libgen.h>

#include "installboot.h"

#define MINIMUM(a, b)	(((a) < (b)) ? (a) : (b))

#define BUFSIZE 512

int
filecopy(const char *srcfile, const char *dstfile)
{
	struct stat sb;
	ssize_t sz, n;
	int sfd, dfd;
	char *buf;

	sfd = open(srcfile, O_RDONLY);
	if (sfd == -1) {
		warn("open %s", srcfile);
		return (-1);
	}
	if (fstat(sfd, &sb) == -1) {
		warn("fstat");
		return (-1);
	}
	sz = sb.st_size;

	dfd = open(dstfile, O_WRONLY|O_CREAT, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
	if (dfd == -1) {
		warn("open %s", dstfile);
		return (-1);
	}
	if (fchown(dfd, 0, 0) == -1)
		if (errno != EINVAL) {
			warn("chown");
			return (-1);
		}
	if (fchmod(dfd, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH) == -1) {
		warn("chmod");
		return (-1);
	}

	if ((buf = malloc(BUFSIZE)) == NULL) {
		warn("malloc");
		return (-1);
	}

	while (sz > 0) {
		n = MINIMUM(sz, BUFSIZE);
		if ((n = read(sfd, buf, n)) == -1) {
			warn("read");
			free(buf);
			return (-1);
		}
		sz -= n;
		if (write(dfd, buf, n) != n) {
			warn("write");
			free(buf);
			return (-1);
		}
	}

	ftruncate(dfd, sb.st_size);

	close(dfd);
	close(sfd);
	free(buf);

	return (0);
}

char *
fileprefix(const char *base, const char *path)
{
	char *r = NULL, *d, *b, *s;
	int n;

	if ((s = malloc(PATH_MAX)) == NULL) {
		warn("malloc");
		return (NULL);
	}
	n = snprintf(s, PATH_MAX, "%s/%s", base, path);
	if (n < 0 || n >= PATH_MAX) {
		warn("snprintf");
		goto err;
	}
	if ((d = dirname(s)) == NULL) {
		warn("dirname");
		goto err;
	}
	if ((r = realpath(d, NULL)) == NULL) {
		warn("realpath");
		goto err;
	}
	if ((b = basename(s)) == NULL) {
		warn("basename");
		goto err;
	}
	n = snprintf(s, PATH_MAX, "%s/%s", r, b);
	if (n < 0 || n >= PATH_MAX) {
		warn("snprintf");
		goto err;
	}
	free(r);
	return (s);

err:
	free(s);
	free(r);
	return (NULL);
}

int
fileprintf(const char *filename, const char *fmt, ...)
{
	va_list ap;
	int fd, ret;
	int rslt = -1;

	fd = open(filename, O_WRONLY|O_CREAT|O_TRUNC,
	    S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
	if (fd == -1) {
		warn("open %s", filename);
		return (-1);
	}
	if (fchown(fd, 0, 0) == -1) {
		if (errno != EINVAL) {
			warn("chown");
			goto err;
		}
	}
	if (fchmod(fd, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH) == -1) {
		warn("chmod");
		goto err;
	}

	va_start(ap, fmt);
	ret = vdprintf(fd, fmt, ap);
	va_end(ap);

	if (ret < 0) {
		warn("vdprintf");
		goto err;
	}

	rslt = 0;

err:
	close(fd);
	return (rslt);
}

/*
 * Adapted from Hacker's Delight crc32b().
 *
 * To quote http://www.hackersdelight.org/permissions.htm :
 *
 * "You are free to use, copy, and distribute any of the code on
 *  this web site, whether modified by you or not. You need not give
 *  attribution. This includes the algorithms (some of which appear
 *  in Hacker's Delight), the Hacker's Assistant, and any code submitted
 *  by readers. Submitters implicitly agree to this."
 */
u_int32_t
crc32(const u_char *buf, const u_int32_t size)
{
	int j;
	u_int32_t i, byte, crc, mask;

	crc = 0xFFFFFFFF;

	for (i = 0; i < size; i++) {
		byte = buf[i];			/* Get next byte. */
		crc = crc ^ byte;
		for (j = 7; j >= 0; j--) {	/* Do eight times. */
			mask = -(crc & 1);
			crc = (crc >> 1) ^ (0xEDB88320 & mask);
		}
	}

	return ~crc;
}
