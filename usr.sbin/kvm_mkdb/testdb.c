/*	$OpenBSD: testdb.c,v 1.10 2019/06/28 13:32:48 deraadt Exp $	*/

/*-
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/sysctl.h>

#include <db.h>
#include <fcntl.h>
#include <kvm.h>
#include <limits.h>
#include <paths.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "extern.h"

/* Return true if the db file is valid, else false */
int
testdb(char *dbname)
{
	DBT rec;
	DB *db = NULL;
	size_t kversionlen;
	char kversion[LINE_MAX];
	int mib[2], ret = 0;

	/* Read version string of running kernel */
	mib[0] = CTL_KERN;
	mib[1] = KERN_VERSION;
	kversionlen = sizeof(kversion);
	if (sysctl(mib, 2, kversion, &kversionlen, NULL, 0) == -1)
		goto close;

	/* Read the version out of the database */
	if ((db = dbopen(dbname, O_RDONLY, 0, DB_HASH, NULL)) == NULL)
		goto close;
	rec.data = VRS_KEY;
	rec.size = sizeof(VRS_KEY) - 1;
	if ((db->get)(db, &rec, &rec, 0))
		goto close;
	if (rec.data == NULL || rec.size > kversionlen)
		goto close;

	/* If they match, we win */
	ret = memcmp(kversion, rec.data, rec.size) == 0;

close:
	if (db)
		(void)(db->close)(db);
	return (ret);
}
