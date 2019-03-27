/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997 Doug Rabson
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

#include <err.h>
#include <libutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/module.h>
#include <sys/linker.h>
#include <strings.h>

#define	POINTER_WIDTH	((int)(sizeof(void *) * 2 + 2))

static int showdata = 0;

static void
printmod(int modid)
{
    struct module_stat stat;

    bzero(&stat, sizeof(stat));
    stat.version = sizeof(struct module_stat);
    if (modstat(modid, &stat) < 0)
	warn("can't stat module id %d", modid);
    else
	if (showdata) {
	    printf("\t\t%3d %s (%d, %u, 0x%lx)\n", stat.id, stat.name, 
	        stat.data.intval, stat.data.uintval, stat.data.ulongval);
	} else {
		printf("\t\t%3d %s\n", stat.id, stat.name);
	}
}

static void
printfile(int fileid, int verbose, int humanized)
{
    struct kld_file_stat stat;
    int modid;
    char buf[5];

    stat.version = sizeof(struct kld_file_stat);
    if (kldstat(fileid, &stat) < 0) {
	err(1, "can't stat file id %d", fileid);
    } else {
	if (humanized) {
		humanize_number(buf, sizeof(buf), stat.size,
		    "", HN_AUTOSCALE, HN_DECIMAL | HN_NOSPACE);

		printf("%2d %4d %*p %5s %s",
		    stat.id, stat.refs, POINTER_WIDTH, stat.address, buf,
		    stat.name);
	} else {
		printf("%2d %4d %*p %8zx %s",
		    stat.id, stat.refs, POINTER_WIDTH, stat.address, stat.size,
		    stat.name);
	}
    }

    if (verbose) {
	printf(" (%s)\n", stat.pathname);
	printf("\tContains modules:\n");
	printf("\t\t Id Name\n");
	for (modid = kldfirstmod(fileid); modid > 0;
	     modid = modfnext(modid))
	    printmod(modid);
    } else
	printf("\n");
}

static void
usage(void)
{
    fprintf(stderr, "usage: kldstat [-d] [-h] [-q] [-v] [-i id] [-n filename]\n");
    fprintf(stderr, "       kldstat [-d] [-q] [-m modname]\n");
    exit(1);
}

int
main(int argc, char** argv)
{
    int c;
    int humanized = 0;
    int verbose = 0;
    int fileid = 0;
    int quiet = 0;
    char* filename = NULL;
    char* modname = NULL;
    char* p;

    while ((c = getopt(argc, argv, "dhi:m:n:qv")) != -1)
	switch (c) {
	case 'd':
	    showdata = 1;
	    break;
	case 'h':
	    humanized = 1;
	    break;
	case 'i':
	    fileid = (int)strtoul(optarg, &p, 10);
	    if (*p != '\0')
		usage();
	    break;
	case 'm':
	    modname = optarg;
	    break;
	case 'n':
	    filename = optarg;
	    break;
	case 'q':
	    quiet = 1;
	    break;
	case 'v':
	    verbose = 1;
	    break;
	default:
	    usage();
	}
    argc -= optind;
    argv += optind;

    if (argc != 0)
	usage();

    if (modname != NULL) {
	int modid;
	struct module_stat stat;

	if ((modid = modfind(modname)) < 0) {
	    if (!quiet)
		warn("can't find module %s", modname);
	    return 1;
	} else if (quiet) {
	    return 0;
	}

	stat.version = sizeof(struct module_stat);
	if (modstat(modid, &stat) < 0)
	    warn("can't stat module id %d", modid);
	else {
		if (showdata) {
		    printf("Id  Refs Name data..(int, uint, ulong)\n");
		    printf("%3d %4d %s (%d, %u, 0x%lx)\n", stat.id, stat.refs, stat.name, 
		        stat.data.intval, stat.data.uintval, stat.data.ulongval);
		} else {
		    printf("Id  Refs Name\n");
		    printf("%3d %4d %s\n", stat.id, stat.refs, stat.name);
		}
	}

	return 0;
    }

    if (filename != NULL) {
	if ((fileid = kldfind(filename)) < 0) {
	    if (!quiet)
		warn("can't find file %s", filename);
	    return 1;
	} else if (quiet) {
	    return 0;
	}
    }

    if (humanized)
	    printf("Id Refs Address%*c %5s Name\n", POINTER_WIDTH - 7, ' ', "Size");
    else
	    printf("Id Refs Address%*c %8s Name\n", POINTER_WIDTH - 7, ' ', "Size");
    if (fileid != 0)
	printfile(fileid, verbose, humanized);
    else
	for (fileid = kldnext(0); fileid > 0; fileid = kldnext(fileid))
	    printfile(fileid, verbose, humanized);

    return 0;
}
