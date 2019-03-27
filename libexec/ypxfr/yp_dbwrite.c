/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1995
 *	Bill Paul <wpaul@ctr.columbia.edu>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR CONTRIBUTORS BE LIABLE
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

#include <db.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <rpcsvc/yp.h>
#include "ypxfr_extern.h"

#define PERM_SECURE (S_IRUSR|S_IWUSR)

/*
 * Open a DB database read/write
 */
DB *
yp_open_db_rw(const char *domain, const char *map, const int flags)
{
	DB *dbp;
	char buf[1025];


	yp_errno = YP_TRUE;

	if (map[0] == '.' || strchr(map, '/')) {
		yp_errno = YP_BADARGS;
		return (NULL);
	}

#define FLAGS O_RDWR|O_EXLOCK|O_EXCL|O_CREAT

	snprintf(buf, sizeof(buf), "%s/%s/%s", yp_dir, domain, map);
	dbp = dbopen(buf,flags ? flags : FLAGS,PERM_SECURE,DB_HASH,&openinfo);

	if (dbp == NULL) {
		switch (errno) {
		case ENOENT:
			yp_errno = YP_NOMAP;
			break;
		case EFTYPE:
			yp_errno = YP_BADDB;
			break;
		default:
			yp_errno = YP_YPERR;
			break;
		}
	}

	return (dbp);
}

int
yp_put_record(DB *dbp, DBT *key, DBT *data, int allow_overwrite)
{
	int rval;

	if ((rval = (dbp->put)(dbp,key,data, allow_overwrite ? 0 :
							R_NOOVERWRITE))) {
		switch (rval) {
		case 1:
			return(YP_FALSE);
			break;
		case -1:
		default:
			(void)(dbp->close)(dbp);
			return(YP_BADDB);
			break;
		}
	}

	return(YP_TRUE);
}
