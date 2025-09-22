/*	$OpenBSD: db.c,v 1.9 2015/01/16 06:40:23 deraadt Exp $ */

/*
 * Copyright (c) 1997 Mats O Jansson <moj@stacken.kth.se>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>
#include <db.h>
#include <fcntl.h>
#include <stdio.h>
#include "db.h"
#include "ypdb.h"

/*
 * This module was created to be able to read database files created
 * by sendmail -bi.
 */

int
db_hash_list_database(char *database)
{
	DB *db;
	int  status;
	DBT key, val;
	char path[PATH_MAX];

	snprintf(path, sizeof(path), "%s%s", database, ".db");

	db = dbopen(path, O_RDONLY, 0, DB_HASH, NULL);
	if (db != NULL) {
		status = db->seq(db, &key, &val, R_FIRST);
		while (status == 0) {
			printf("%*.*s %*.*s\n",
			    (int)key.size-1, (int)key.size-1, (char *)key.data,
			    (int)val.size-1, (int)val.size-1, (char *)val.data);
			status = db->seq(db, &key, &val, R_NEXT);
		}
		db->close(db);
		return(1);
	}
	return(0);
}
