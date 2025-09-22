/* $OpenBSD: revnetgroup.c,v 1.11 2013/12/05 14:20:53 jca Exp $ */
/*
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
 *
 * reverse netgroup map generator program
 *
 * Written by Bill Paul <wpaul@ctr.columbia.edu>
 * Center for Telecommunications Research
 * Columbia University, New York City
 *
 *	$FreeBSD: revnetgroup.c,v 1.7 1997/03/28 15:48:15 imp Exp $
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <util.h>
#include <errno.h>
#include <err.h>
#include "hash.h"

/* Default location of netgroup file. */
char *netgroup = "/etc/netgroup";

/* Stored hash table version of 'forward' netgroup database. */
struct group_entry *gtable[TABLESIZE];

/*
 * Stored hash table of 'reverse' netgroup member database
 * which we will construct.
 */
struct member_entry *mtable[TABLESIZE];

static void
usage(void)
{
	fprintf (stderr,"usage: revnetgroup -h | -u [-f netgroup_file]\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	FILE *fp;
	char *readbuf;
	struct group_entry *gcur;
	struct member_entry *mcur;
	char *host, *user, *domain;
	extern char *optarg;
	int ch;
	char *key = NULL, *data = NULL;
	int hosts = -1, i;

	if (argc < 2)
		usage();

	while ((ch = getopt(argc, argv, "uhf:")) != -1) {
		switch (ch) {
		case 'u':
			if (hosts != -1) {
				warnx("please use only one of -h or -u");
				usage();
			}
			hosts = 0;
			break;
		case 'h':
			if (hosts != -1) {
				warnx("please use only one of -u or -h");
				usage();
			}
			hosts = 1;
			break;
		case 'f':
			netgroup = optarg;
			break;
		default:
			usage();
			break;
		}
	}

	if (hosts == -1)
		usage();

	if (strcmp(netgroup, "-")) {
		if ((fp = fopen(netgroup, "r")) == NULL) {
			err(1, "%s", netgroup);
		}
	} else {
		fp = stdin;
	}

	/* Stuff all the netgroup names and members into a hash table. */
	while ((readbuf = fparseln(fp, NULL, NULL, NULL, 0)) != NULL) {
		data = strpbrk(readbuf, " \t");
		if (data == NULL) {
			free(readbuf);
			continue;
		}
		*data = '\0';
		++data;
		key = readbuf;
		ngstore(gtable, key, data);
		free(readbuf);
	}

	fclose(fp);

	/*
	 * Find all members of each netgroup and keep track of which
	 * group they belong to.
	 */
	for (i = 0; i < TABLESIZE; i++) {
		gcur = gtable[i];
		while (gcur) {
			__setnetgrent(gcur->key);
			while (__getnetgrent(&host, &user, &domain) != 0) {
				if (hosts) {
					if (!(host && !strcmp(host,"-"))) {
						mstore(mtable,
						    host ? host : "*",
						    gcur->key,
						    domain ? domain : "*");
					}
				} else {
					if (!(user && !strcmp(user,"-"))) {
						mstore(mtable,
						    user ? user : "*",
						    gcur->key,
						    domain ? domain : "*");
					}
				}
			}
			gcur = gcur->next;
		}
	}

	/* Release resources used by the netgroup parser code. */
	__endnetgrent();

	/* Spew out the results. */
	for (i = 0; i < TABLESIZE; i++) {
		mcur = mtable[i];
		while (mcur) {
			struct grouplist *tmp;
			printf ("%s.%s\t", mcur->key, mcur->domain);
			tmp = mcur->groups;
			while (tmp) {
				printf ("%s", tmp->groupname);
				tmp = tmp->next;
				if (tmp)
					printf(",");
			}
			mcur = mcur->next;
			printf ("\n");
		}
	}

	/* Let the OS free all our resources. */
	exit(0);
}
