/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Ed Schouten <ed@FreeBSD.org>
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

#include "namespace.h"
#include <sys/endian.h>
#include <sys/param.h>
#include <sys/time.h>
#include <stdlib.h>
#include <string.h>
#include <utmpx.h>
#include "utxdb.h"
#include "un-namespace.h"

#define	UTOF_STRING(ut, fu, field) do { \
	strncpy((fu)->fu_ ## field, (ut)->ut_ ## field,		\
	    MIN(sizeof (fu)->fu_ ## field, sizeof (ut)->ut_ ## field));	\
} while (0)
#define	UTOF_ID(ut, fu) do { \
	memcpy((fu)->fu_id, (ut)->ut_id,				\
	    MIN(sizeof (fu)->fu_id, sizeof (ut)->ut_id));		\
} while (0)
#define	UTOF_PID(ut, fu) do { \
	(fu)->fu_pid = htobe32((ut)->ut_pid);				\
} while (0)
#define	UTOF_TYPE(ut, fu) do { \
	(fu)->fu_type = (ut)->ut_type;					\
} while (0)
#define	UTOF_TV(fu) do { \
	struct timeval tv;						\
	gettimeofday(&tv, NULL);					\
	(fu)->fu_tv = htobe64((uint64_t)tv.tv_sec * 1000000 +		\
	    (uint64_t)tv.tv_usec);					\
} while (0)

void
utx_to_futx(const struct utmpx *ut, struct futx *fu)
{

	memset(fu, 0, sizeof *fu);

	switch (ut->ut_type) {
	case BOOT_TIME:
	case OLD_TIME:
	case NEW_TIME:
	/* Extension: shutdown time. */
	case SHUTDOWN_TIME:
		break;
	case USER_PROCESS:
		UTOF_ID(ut, fu);
		UTOF_STRING(ut, fu, user);
		UTOF_STRING(ut, fu, line);
		/* Extension: host name. */
		UTOF_STRING(ut, fu, host);
		UTOF_PID(ut, fu);
		break;
	case INIT_PROCESS:
		UTOF_ID(ut, fu);
		UTOF_PID(ut, fu);
		break;
	case LOGIN_PROCESS:
		UTOF_ID(ut, fu);
		UTOF_STRING(ut, fu, user);
		UTOF_STRING(ut, fu, line);
		UTOF_PID(ut, fu);
		break;
	case DEAD_PROCESS:
		UTOF_ID(ut, fu);
		UTOF_PID(ut, fu);
		break;
	default:
		fu->fu_type = EMPTY;
		return;
	}

	UTOF_TYPE(ut, fu);
	UTOF_TV(fu);
}

#define	FTOU_STRING(fu, ut, field) do { \
	strncpy((ut)->ut_ ## field, (fu)->fu_ ## field,		\
	    MIN(sizeof (ut)->ut_ ## field - 1, sizeof (fu)->fu_ ## field)); \
} while (0)
#define	FTOU_ID(fu, ut) do { \
	memcpy((ut)->ut_id, (fu)->fu_id,				\
	    MIN(sizeof (ut)->ut_id, sizeof (fu)->fu_id));		\
} while (0)
#define	FTOU_PID(fu, ut) do { \
	(ut)->ut_pid = be32toh((fu)->fu_pid);				\
} while (0)
#define	FTOU_TYPE(fu, ut) do { \
	(ut)->ut_type = (fu)->fu_type;					\
} while (0)
#define	FTOU_TV(fu, ut) do { \
	uint64_t t;							\
	t = be64toh((fu)->fu_tv);					\
	(ut)->ut_tv.tv_sec = t / 1000000;				\
	(ut)->ut_tv.tv_usec = t % 1000000;				\
} while (0)

struct utmpx *
futx_to_utx(const struct futx *fu)
{
#ifdef __NO_TLS
	static struct utmpx *ut;
#else
	static _Thread_local struct utmpx *ut;
#endif

	if (ut == NULL) {
		ut = calloc(1, sizeof *ut);
		if (ut == NULL)
			return (NULL);
	} else
		memset(ut, 0, sizeof *ut);

	switch (fu->fu_type) {
	case BOOT_TIME:
	case OLD_TIME:
	case NEW_TIME:
	/* Extension: shutdown time. */
	case SHUTDOWN_TIME:
		break;
	case USER_PROCESS:
		FTOU_ID(fu, ut);
		FTOU_STRING(fu, ut, user);
		FTOU_STRING(fu, ut, line);
		/* Extension: host name. */
		FTOU_STRING(fu, ut, host);
		FTOU_PID(fu, ut);
		break;
	case INIT_PROCESS:
		FTOU_ID(fu, ut);
		FTOU_PID(fu, ut);
		break;
	case LOGIN_PROCESS:
		FTOU_ID(fu, ut);
		FTOU_STRING(fu, ut, user);
		FTOU_STRING(fu, ut, line);
		FTOU_PID(fu, ut);
		break;
	case DEAD_PROCESS:
		FTOU_ID(fu, ut);
		FTOU_PID(fu, ut);
		break;
	default:
		ut->ut_type = EMPTY;
		return (ut);
	}

	FTOU_TYPE(fu, ut);
	FTOU_TV(fu, ut);
	return (ut);
}
