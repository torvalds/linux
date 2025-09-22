/*	$OpenBSD: posix_time.h,v 1.1 2024/02/18 16:28:38 tb Exp $ */
/*
 * Copyright (c) 2022, Google Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef OPENSSL_HEADER_POSIX_TIME_H
#define OPENSSL_HEADER_POSIX_TIME_H

#include <stdint.h>
#include <time.h>

#if defined(__cplusplus)
extern "C" {
#endif

/*
 * OPENSSL_posix_to_tm converts a int64_t POSIX time value in |time|, which must
 * be in the range of year 0000 to 9999, to a broken out time value in |tm|. It
 * returns one on success and zero on error.
 */
int OPENSSL_posix_to_tm(int64_t time, struct tm *out_tm);

/*
 * OPENSSL_tm_to_posix converts a time value between the years 0 and 9999 in
 * |tm| to a POSIX time value in |out|. One is returned on success, zero is
 * returned on failure. It is a failure if |tm| contains out of range values.
 */
int OPENSSL_tm_to_posix(const struct tm *tm, int64_t *out);

/*
 * OPENSSL_timegm converts a time value between the years 0 and 9999 in |tm| to
 * a time_t value in |out|. One is returned on success, zero is returned on
 * failure. It is a failure if the converted time can not be represented in a
 * time_t, or if the tm contains out of range values.
 */
int OPENSSL_timegm(const struct tm *tm, time_t *out);

#if defined(__cplusplus)
}  /* extern C */
#endif

#endif  /* OPENSSL_HEADER_POSIX_TIME_H */
