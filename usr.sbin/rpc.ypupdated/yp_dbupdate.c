/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1996
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

#include <sys/fcntl.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <db.h>
#include <unistd.h>
#include <rpcsvc/ypclnt.h>
#include <rpcsvc/ypupdate_prot.h>
#include "ypxfr_extern.h"
#include "ypupdated_extern.h"

static int
yp_domake(char *map, char *domain)
{
	int pid;

	switch ((pid = fork())) {
	case 0:
		execlp(MAP_UPDATE_PATH, MAP_UPDATE, map, domain, (char *)NULL);
		yp_error("couldn't exec map update process: %s",
						strerror(errno));
		exit(1);
		break;
	case -1:
		yp_error("fork() failed: %s", strerror(errno));
		return(YPERR_YPERR);
		break;
	default:
		children++;
		break;
	}

	return(0);
}

int
ypmap_update(char *netname, char *map, unsigned int op, unsigned int keylen,
    char *keyval, unsigned int datlen, char *datval)
{
	DB *dbp;
	DBT key = { NULL, 0 }, data = { NULL, 0 };
	char *yp_last = "YP_LAST_MODIFIED";
	char yplastbuf[YPMAXRECORD];
	char *domptr;
	int rval = 0;

	if ((domptr = strchr(netname, '@')) == NULL)
		return(ERR_ACCESS);
	domptr++;


	dbp = yp_open_db_rw(domptr, map, O_RDWR);
	if (dbp == NULL)
		return(ERR_DBASE);

	key.data = keyval;
	key.size = keylen;
	data.data = datval;
	data.size = datlen;

	switch (op) {
	case YPOP_DELETE: /* delete this entry */
		rval = yp_del_record(dbp, &key);
		if (rval == YP_TRUE)
			rval = 0;
		break;
	case YPOP_INSERT: /* add, do not change */
		rval = yp_put_record(dbp, &key, &data, 0);
		if (rval == YP_TRUE)
			rval = 0;
		break;
	case YPOP_STORE: /* add, or change */
		rval = yp_put_record(dbp, &key, &data, 1);
		if (rval == YP_TRUE)
			rval = 0;
		break;
	case YPOP_CHANGE: /* change, do not add */
		if (yp_get_record(domptr, map, &key, &data, 0) != YP_TRUE) {
			rval = ERR_KEY;
			break;
		}
		rval = yp_put_record(dbp, &key, &data, 1);
		if (rval == YP_TRUE)
			rval = 0;
		break;
	default:
		yp_error("unknown update command: (%d)", op);
	}

	if (rval) {
		(void)(dbp->close)(dbp);
		return(rval);
	}

	snprintf(yplastbuf, sizeof(yplastbuf), "%jd", (intmax_t)time(NULL));
	key.data = yp_last;
	key.size = strlen(yp_last);
	data.data = (char *)&yplastbuf;
	data.size = strlen(yplastbuf);
	if (yp_put_record(dbp, &key, &data, 1) != YP_TRUE) {
		yp_error("failed to update timestamp in %s/%s", domptr, map);
		(void)(dbp->close)(dbp);
		return(ERR_DBASE);
	}

	(void)(dbp->close)(dbp);
	return(yp_domake(map, domptr));
}
