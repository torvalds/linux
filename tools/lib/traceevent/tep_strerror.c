// SPDX-License-Identifier: LGPL-2.1
#undef _GNU_SOURCE
#include <string.h>
#include <stdio.h>

#include "event-parse.h"

#undef _PE
#define _PE(code, str) str
static const char * const tep_error_str[] = {
	TEP_ERRORS
};
#undef _PE

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
int tep_strerror(struct tep_handle *tep __maybe_unused,
		 enum tep_errno errnum, char *buf, size_t buflen)
{
	const char *msg;
	int idx;

	if (!buflen)
		return 0;

	if (errnum >= 0) {
		int err = strerror_r(errnum, buf, buflen);
		buf[buflen - 1] = 0;
		return err;
	}

	if (errnum <= __TEP_ERRNO__START ||
	    errnum >= __TEP_ERRNO__END)
		return -1;

	idx = errnum - __TEP_ERRNO__START - 1;
	msg = tep_error_str[idx];
	snprintf(buf, buflen, "%s", msg);

	return 0;
}
