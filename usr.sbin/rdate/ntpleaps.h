/*	$OpenBSD: ntpleaps.h,v 1.4 2007/11/25 16:40:04 jmc Exp $	*/

/*
 * Copyright (c) 2002 Thorsten Glaser. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *    - Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    - Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

/* Leap second support for SNTP clients
 * This header file and its corresponding C file provide generic
 * ability for NTP or SNTP clients to correctly handle leap seconds
 * by reading them from an always existing file and subtracting the
 * leap seconds from the NTP return value before setting the posix
 * clock. This is fairly portable between operating systems and may
 * be used for patching other ntp clients, too. The tzfile used is:
 * /usr/share/zoneinfo/right/UTC which is available on any unix-like
 * platform with the Olson tz library, which is necessary to get real
 * leap second zoneinfo files and userland support anyways.
 */

#ifndef _NTPLEAPS_H
#define _NTPLEAPS_H

/* Offset between struct timeval.tv_sec and a tai64_t */
#define	NTPLEAPS_OFFSET	(4611686018427387914ULL)

/* Hide this ugly value from programmes */
#define	SEC_TO_TAI64(s)	(NTPLEAPS_OFFSET + (u_int64_t)(s))
#define	TAI64_TO_SEC(t)	((t) - NTPLEAPS_OFFSET)

/* Initializes the leap second table. Does not need to be called
 * before usage of the subtract function, but calls ntpleaps_read.
 * returns 0 on success, -1 on error (displays a warning on stderr)
 */
int ntpleaps_init(void);

/* Re-reads the leap second table, thus consuming quite much time.
 * Ought to be called from within daemons at least once a month to
 * ensure the in-memory table is always up-to-date.
 * returns 0 on success, -1 on error (leap seconds will not be available)
 */
int ntpleaps_read(void);

/* Subtracts leap seconds from the given value (converts NTP time
 * to posix clock tick time.
 * returns 0 on success, -1 on error (time is unchanged), 1 on leap second
 */
int ntpleaps_sub(u_int64_t *);

#endif
