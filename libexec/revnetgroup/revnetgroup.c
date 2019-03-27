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
 *
 * reverse netgroup map generator program
 *
 * Written by Bill Paul <wpaul@ctr.columbia.edu>
 * Center for Telecommunications Research
 * Columbia University, New York City
 */

#ifndef lint
static const char rcsid[] =
  "$FreeBSD$";
#endif /* not lint */

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
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
	fprintf (stderr,"usage: revnetgroup -u | -h [-f netgroup_file]\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	FILE *fp;
	char readbuf[LINSIZ];
	struct group_entry *gcur;
	struct member_entry *mcur;
	char *host, *user, *domain;
	int ch;
	char *key = NULL, *data = NULL;
	int hosts = -1, i;

	if (argc < 2)
		usage();

	while ((ch = getopt(argc, argv, "uhf:")) != -1) {
		switch(ch) {
		case 'u':
			if (hosts != -1) {
				warnx("please use only one of -u or -h");
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
	while (fgets(readbuf, LINSIZ, fp)) {
		if (readbuf[0] == '#')
			continue;
		/* handle backslash line continuations */
		while(readbuf[strlen(readbuf) - 2] == '\\') {
			fgets((char *)&readbuf[strlen(readbuf) - 2],
					sizeof(readbuf) - strlen(readbuf), fp);
		}
		data = NULL;
		if ((data = (char *)(strpbrk(readbuf, " \t") + 1)) < (char *)2)
			continue;
		key = (char *)&readbuf;
		*(data - 1) = '\0';
		store(gtable, key, data);
	}

	fclose(fp);

	/*
	 * Find all members of each netgroup and keep track of which
	 * group they belong to.
	 */
	for (i = 0; i < TABLESIZE; i++) {
		gcur = gtable[i];
		while(gcur) {
			__setnetgrent(gcur->key);
			while(__getnetgrent(&host, &user, &domain) != 0) {
				if (hosts ? host && strcmp(host,"-") : user && strcmp(user, "-"))
					mstore(mtable, hosts ? host : user, gcur->key, domain);
			}
			gcur = gcur->next;
		}
	}

	/* Release resources used by the netgroup parser code. */
	__endnetgrent();

	/* Spew out the results. */
	for (i = 0; i < TABLESIZE; i++) {
		mcur = mtable[i];
		while(mcur) {
			struct grouplist *tmp;
			printf ("%s.%s\t", mcur->key, mcur->domain);
			tmp = mcur->groups;
			while(tmp) {
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
