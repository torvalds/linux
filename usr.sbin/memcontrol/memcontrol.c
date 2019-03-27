/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Michael Smith <msmith@freebsd.org>
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
 *
 * $FreeBSD$
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/memrange.h>

#include <err.h>
#include <fcntl.h>
#include <inttypes.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static struct {
    const char	*name;
    int		val;
    int		kind;
#define MDF_SETTABLE	(1<<0)
} attrnames[] = {
    {"uncacheable",	MDF_UNCACHEABLE,	MDF_SETTABLE},
    {"write-combine",	MDF_WRITECOMBINE,	MDF_SETTABLE},
    {"write-through",	MDF_WRITETHROUGH,	MDF_SETTABLE},
    {"write-back",	MDF_WRITEBACK,		MDF_SETTABLE},
    {"write-protect",	MDF_WRITEPROTECT,	MDF_SETTABLE},
    {"force",		MDF_FORCE,		MDF_SETTABLE},
    {"unknown",		MDF_UNKNOWN,		0},
    {"fixed-base",	MDF_FIXBASE,		0},
    {"fixed-length",	MDF_FIXLEN,		0},
    {"set-by-firmware",	MDF_FIRMWARE,		0},
    {"active",		MDF_ACTIVE,		MDF_SETTABLE},
    {"bogus",		MDF_BOGUS,		0},
    {NULL,		0,			0}
};

static void	listfunc(int memfd, int argc, char *argv[]);
static void	setfunc(int memfd, int argc, char *argv[]);
static void	clearfunc(int memfd, int argc, char *argv[]);
static void	helpfunc(int memfd, int argc, char *argv[]);
static void	help(const char *what);

static struct {
    const char	*cmd;
    const char	*desc;
    void	(*func)(int memfd, int argc, char *argv[]);
} functions[] = {
    {"list",	
     "List current memory range attributes\n"
     "    list [-a]\n"
     "        -a    list all range slots, even those that are inactive",
     listfunc},
    {"set",	
     "Set memory range attributes\n"
     "    set -b <base> -l <length> -o <owner> <attribute>\n"
     "        <base>      memory range base address\n"
     "        <length>    length of memory range in bytes, power of 2\n"
     "        <owner>     text identifier for this setting (7 char max)\n"
     "        <attribute> attribute(s) to be applied to this range:\n"
     "                        uncacheable\n"
     "                        write-combine\n"
     "                        write-through\n"
     "                        write-back\n"
     "                        write-protect",
     setfunc},
    {"clear",	
     "Clear memory range attributes\n"
     "    clear -o <owner>\n"
     "        <owner>     all ranges with this owner will be cleared\n"
     "    clear -b <base> -l <length>\n"
     "        <base>      memory range base address\n"
     "        <length>    length of memory range in bytes, power of 2\n"
     "                    Base and length must exactly match an existing range",
     clearfunc},
    {NULL,	NULL,					helpfunc}
};

int
main(int argc, char *argv[])
{
    int		i, memfd;

    if (argc < 2) {
	help(NULL);
    } else {
	if ((memfd = open(_PATH_MEM, O_RDONLY)) == -1)
	    err(1, "can't open %s", _PATH_MEM);

	for (i = 0; functions[i].cmd != NULL; i++)
	    if (!strcmp(argv[1], functions[i].cmd))
		break;
	functions[i].func(memfd, argc - 1, argv + 1);
	close(memfd);
    }
    return(0);
}

static struct mem_range_desc *
mrgetall(int memfd, int *nmr)
{
    struct mem_range_desc	*mrd;
    struct mem_range_op		mro;

    mro.mo_arg[0] = 0;
    if (ioctl(memfd, MEMRANGE_GET, &mro))
	err(1, "can't size range descriptor array");
    
    *nmr = mro.mo_arg[0];
    mrd = malloc(*nmr * sizeof(struct mem_range_desc));
    if (mrd == NULL)
	errx(1, "can't allocate %zd bytes for %d range descriptors", 
	     *nmr * sizeof(struct mem_range_desc), *nmr);

    mro.mo_arg[0] = *nmr;
    mro.mo_desc = mrd;
    if (ioctl(memfd, MEMRANGE_GET, &mro))
	err(1, "can't fetch range descriptor array");

    return(mrd);
}


static void
listfunc(int memfd, int argc, char *argv[])
{
    struct mem_range_desc	*mrd;
    int				nd, i, j;
    int				ch;
    int				showall = 0;
    char			*owner;

    owner = NULL;
    while ((ch = getopt(argc, argv, "ao:")) != -1)
	switch(ch) {
	case 'a':
	    showall = 1;
	    break;
	case 'o':
	    owner = strdup(optarg);
	    break;
	case '?':
	default:
	    help("list");
	}

    mrd = mrgetall(memfd, &nd);
    
    for (i = 0; i < nd; i++) {
	if (!showall && !(mrd[i].mr_flags & MDF_ACTIVE))
	    continue;
	if (owner && strcmp(mrd[i].mr_owner, owner))
	    continue;
	printf("0x%" PRIx64 "/0x%" PRIx64 " %.8s ", mrd[i].mr_base, mrd[i].mr_len, 
	       mrd[i].mr_owner[0] ? mrd[i].mr_owner : "-");
	for (j = 0; attrnames[j].name != NULL; j++)
	    if (mrd[i].mr_flags & attrnames[j].val)
		printf("%s ", attrnames[j].name);
	printf("\n");
    }
    free(mrd);
    if (owner)
	free(owner);
}

static void
setfunc(int memfd, int argc, char *argv[])
{
    struct mem_range_desc	mrd;
    struct mem_range_op		mro;
    int				i;
    int				ch;
    char			*ep;

    mrd.mr_base = 0;
    mrd.mr_len = 0;
    mrd.mr_flags = 0;
    strcpy(mrd.mr_owner, "user");
    while ((ch = getopt(argc, argv, "b:l:o:")) != -1)
	switch(ch) {
	case 'b':
	    mrd.mr_base = strtouq(optarg, &ep, 0);
	    if ((ep == optarg) || (*ep != 0))
		help("set");
	    break;
	case 'l':
	    mrd.mr_len = strtouq(optarg, &ep, 0);
	    if ((ep == optarg) || (*ep != 0))
		help("set");
	    break;
	case 'o':
	    if ((*optarg == 0) || (strlen(optarg) > 7))
		help("set");
	    strcpy(mrd.mr_owner, optarg);
	    break;
	    
	case '?':
	default:
	    help("set");
	}

    if (mrd.mr_len == 0)
	help("set");

    argc -= optind;
    argv += optind;
    
    while(argc--) {
	for (i = 0; attrnames[i].name != NULL; i++) {
	    if (!strcmp(attrnames[i].name, argv[0])) {
		if (!(attrnames[i].kind & MDF_SETTABLE))
		    help("flags");
		mrd.mr_flags |= attrnames[i].val;
		break;
	    }
	}
	if (attrnames[i].name == NULL)
	    help("flags");
	argv++;
    }

    mro.mo_desc = &mrd;
    mro.mo_arg[0] = 0;
    if (ioctl(memfd, MEMRANGE_SET, &mro))
	err(1, "can't set range");
}

static void
clearfunc(int memfd, int argc, char *argv[])
{
    struct mem_range_desc	mrd, *mrdp;
    struct mem_range_op		mro;
    int				i, nd;
    int				ch;
    char			*ep, *owner;

    mrd.mr_base = 0;
    mrd.mr_len = 0;
    owner = NULL;
    while ((ch = getopt(argc, argv, "b:l:o:")) != -1)
	switch(ch) {
	case 'b':
	    mrd.mr_base = strtouq(optarg, &ep, 0);
	    if ((ep == optarg) || (*ep != 0))
		help("clear");
	    break;
	case 'l':
	    mrd.mr_len = strtouq(optarg, &ep, 0);
	    if ((ep == optarg) || (*ep != 0))
		help("clear");
	    break;
	case 'o':
	    if ((*optarg == 0) || (strlen(optarg) > 7))
		help("clear");
	    owner = strdup(optarg);
	    break;
	    
	case '?':
	default:
	    help("clear");
	}

    if (owner != NULL) {
	/* clear-by-owner */
	if ((mrd.mr_base != 0) || (mrd.mr_len != 0))
	    help("clear");

	mrdp = mrgetall(memfd, &nd);
	mro.mo_arg[0] = MEMRANGE_SET_REMOVE;
	for (i = 0; i < nd; i++) {
	    if (!strcmp(owner, mrdp[i].mr_owner) && 
		(mrdp[i].mr_flags & MDF_ACTIVE) &&
		!(mrdp[i].mr_flags & MDF_FIXACTIVE)) {
		
		mro.mo_desc = mrdp + i;
		if (ioctl(memfd, MEMRANGE_SET, &mro))
		    warn("couldn't clear range owned by '%s'", owner);
	    }
	}
    } else if (mrd.mr_len != 0) {
	/* clear-by-base/len */
	mro.mo_arg[0] = MEMRANGE_SET_REMOVE;
	mro.mo_desc = &mrd;
	if (ioctl(memfd, MEMRANGE_SET, &mro))
	    err(1, "couldn't clear range");
    } else {
	help("clear");
    }
}

static void
helpfunc(__unused int memfd, __unused int argc, char *argv[])
{
    help(argv[1]);
}

static void
help(const char *what)
{
    int		i;

    if (what != NULL) {
	/* find a function that matches */
	for (i = 0; functions[i].cmd != NULL; i++)
	    if (!strcmp(what, functions[i].cmd)) {
		fprintf(stderr, "%s\n", functions[i].desc);
		return;
	    }
	fprintf(stderr, "Unknown command '%s'\n", what);
    }
    
    /* print general help */
    fprintf(stderr, "Valid commands are :\n");
    for (i = 0; functions[i].cmd != NULL; i++)
	fprintf(stderr, "    %s\n", functions[i].cmd);
    fprintf(stderr, "Use help <command> for command-specific help\n");
}
