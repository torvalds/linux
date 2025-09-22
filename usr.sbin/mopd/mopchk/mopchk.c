/*	$OpenBSD: mopchk.c,v 1.22 2024/09/20 02:00:46 jsg Exp $	*/

/*
 * Copyright (c) 1995-96 Mats O Jansson.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * mopchk - MOP Check Utility
 *
 * Usage:	mopchk [-av] [file ...]
 */

#include "os.h"
#include "common/common.h"
#include "common/mopdef.h"
#include "common/device.h"
#include "common/pf.h"
#include "common/file.h"

/*
 * The list of all interfaces that are being listened to.
 */
struct if_info *iflist;

void   Usage(void);
void   mopProcess(struct if_info *, u_char *);

int     AllFlag = 0;		/* listen on "all" interfaces  */
int	VersionFlag = 0;	/* Show version */
int	promisc = 0;		/* promisc mode not needed */
extern char *__progname;
extern char version[];

int
main(int argc, char **argv)
{
	struct dllist dl;
	int     op, i;
	char   *filename, *p;
	struct if_info *ii;
	int	error;

	/* All error reporting is done through syslogs. */
	openlog(__progname, LOG_PID | LOG_CONS, LOG_DAEMON);

	opterr = 0;
	while ((op = getopt(argc, argv, "av")) != -1) {
		switch (op) {
		case 'a':
			AllFlag = 1;
			break;
		case 'v':
			VersionFlag = 1;
			break;
		default:
			Usage();
			/* NOTREACHED */
		}
	}
	
	if (VersionFlag)
		printf("%s: Version %s\n", __progname, version);

	if (AllFlag) {
		if (VersionFlag)
			printf("\n");
		iflist = NULL;
		deviceInitAll();
		if (iflist == NULL) {
			printf("No interface\n");
		} else {
			printf("Interface Address\n");
			p = NULL;
			for (ii = iflist; ii; ii = ii->next) {
				if (p != NULL) {
					if (strcmp(p,ii->if_name) == 0)
						continue;
				}	
				printf("%-9s %x:%x:%x:%x:%x:%x\n",
				       ii->if_name,
				       ii->eaddr[0],ii->eaddr[1],ii->eaddr[2],
				       ii->eaddr[3],ii->eaddr[4],ii->eaddr[5]);
				p = ii->if_name;
			}
		}
	}
	
	if (VersionFlag || AllFlag)
		i = 1;
	else
		i = 0;

	while (argc > optind) {
		if (i)	printf("\n");
		i++;
		filename = argv[optind++];
		printf("Checking: %s\n",filename);
		dl.ldfd = open(filename, O_RDONLY);
		if (dl.ldfd == -1) {
			printf("Unknown file.\n");
		} else {
			if ((error = CheckElfFile(dl.ldfd)) == 0) {
				if (GetElf32FileInfo(&dl, INFO_PRINT) < 0 &&
				    GetElf64FileInfo(&dl, INFO_PRINT) < 0) {
					printf("Some failure in GetElfXXFileInfo\n");
				}
			} else if ((error = CheckAOutFile(dl.ldfd)) == 0) {
				if (GetAOutFileInfo(&dl, INFO_PRINT) < 0) {
					printf("Some failure in GetAOutFileInfo\n");
				}
			} else if ((error = CheckMopFile(dl.ldfd)) == 0) {
				if (GetMopFileInfo(&dl, INFO_PRINT) < 0) {
					printf("Some failure in GetMopFileInfo\n");
				}
			}
		}
		(void)close(dl.ldfd);
	}
	return 0;
}

void
Usage(void)
{
	fprintf(stderr, "usage: %s [-av] [file ...]\n", __progname);
	exit(1);
}

/*
 * Process incoming packages, NOT. 
 */
void
mopProcess(struct if_info *ii, u_char *pkt)
{
}

