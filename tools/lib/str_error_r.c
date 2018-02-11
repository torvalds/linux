// SPDX-License-Identifier: GPL-2.0
#undef _GNU_SOURCE
#include <string.h>
#include <stdio.h>
#include <linux/string.h>

/*
 * The tools so far have been using the strerror_r() GNU variant, that returns
 * a string, be it the buffer passed or something else.
 *
 * But that, besides being tricky in cases where we expect that the function
 * using strerror_r() returns the error formatted in a provided buffer (we have
 * to check if it returned something else and copy that instead), breaks the
 * build on systems not using glibc, like Alpine Linux, where musl libc is
 * used.
 *
 * So, introduce yet another wrapper, str_error_r(), that has the GNU
 * interface, but uses the portable XSI variant of strerror_r(), so that users
 * rest asured that the provided buffer is used and it is what is returned.
 */
char *str_error_r(int errnum, char *buf, size_t buflen)
{
	int err = strerror_r(errnum, buf, buflen);
	if (err)
		snprintf(buf, buflen, "INTERNAL ERROR: strerror_r(%d, %p, %zd)=%d", errnum, buf, buflen, err);
	return buf;
}
