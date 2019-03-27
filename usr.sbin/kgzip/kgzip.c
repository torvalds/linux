/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Global Technology Associates, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef lint
static const char rcsid[] =
  "$FreeBSD$";
#endif /* not lint */

#include <sys/types.h>
#include <err.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "kgzip.h"

#define FN_SRC		0	/* Filename: source */
#define FN_OBJ		1	/* Filename: relocatable */
#define FN_KGZ		2	/* Filename: executable */
#define FN_CNT		3	/* Number of filenames */

#define SFX_OBJ 	".o"	/* Filename suffix: relocatable */
#define SFX_KGZ 	".kgz"	/* Filename suffix: executable */
#define SFX_MAX 	5	/* Size of larger filename suffix */

const char *loader = "/usr/lib/kgzldr.o";  /* Default loader */
int format;			/* Output format */

char *tname;			/* Name of temporary file */

static void cleanup(void);
static void mk_fn(int, const char *, const char *, char *[]);
static void usage(void);

/*
 * Compress a kernel.
 */
int
main(int argc, char *argv[])
{
    static char *fn[FN_CNT];
    struct kgz_hdr kh;
    const char *output;
    char *tmpdir;
    int cflag, vflag, c;

    tmpdir = getenv("TMPDIR");
    if (asprintf(&tname, "%s/kgzXXXXXXXXXX",
		 tmpdir == NULL ? _PATH_TMP : tmpdir) == -1)
	errx(1, "Out of memory");
    output = NULL;
    cflag = vflag = 0;
    while ((c = getopt(argc, argv, "cvf:l:o:")) != -1)
	switch (c) {
	case 'c':
	    cflag = 1;
	    break;
	case 'v':
	    vflag = 1;
	    break;
	case 'f':
	    if (!strcmp(optarg, "aout"))
		format = F_AOUT;
	    else if (!strcmp(optarg, "elf"))
		format = F_ELF;
	    else
		errx(1, "%s: Unknown format", optarg);
	    break;
	case 'l':
	    loader = optarg;
	    break;
	case 'o':
	    output = optarg;
	    break;
	default:
	    usage();
	}
    argc -= optind;
    argv += optind;
    if (argc != 1)
	usage();
    atexit(cleanup);
    mk_fn(cflag, *argv, output, fn);
    memset(&kh, 0, sizeof(kh));
    if (fn[FN_SRC]) {
	if (!format)
	    format = F_ELF;
	kgzcmp(&kh, fn[FN_SRC], fn[FN_OBJ]);
    }
    if (!cflag)
	kgzld(&kh, fn[FN_OBJ], fn[FN_KGZ]);
    if (vflag)
	printf("dload=%#x dsize=%#x isize=%#x entry=%#x nsize=%#x\n",
	       kh.dload, kh.dsize, kh.isize, kh.entry, kh.nsize);
    return 0;
}

/*
 * Clean up after processing.
 */
static void
cleanup(void)
{
    if (tname)
	unlink(tname);
}

/*
 * Make the required filenames.
 */
static void
mk_fn(int cflag, const char *f1, const char *f2, char *fn[])
{
    const char *p, *s;
    size_t n;
    int i, fd;

    i = 0;
    s = strrchr(f1, 0);
    n = sizeof(SFX_OBJ) - 1;
    if ((size_t)(s - f1) > n && !memcmp(s - n, SFX_OBJ, n)) {
	s -= n;
	i++;
    }
    fn[i++] = (char *)f1;
    if (i == FN_OBJ && !cflag) {
	if ((fd = mkstemp(tname)) == -1)
	    err(1, NULL);
	close(fd);
	fn[i++] = tname;
    }
    if (!(fn[i] = (char *)f2)) {
	p = (p = strrchr(f1, '/')) ? p + 1 : f1;
	n = (size_t)(s - p);
	if (!(fn[i] = malloc(n + SFX_MAX)))
	    err(1, NULL);
	memcpy(fn[i], p, n);
	strcpy(fn[i] + n, i == FN_OBJ ? SFX_OBJ : SFX_KGZ);
    }
}

/*
 * Display usage information.
 */
static void
usage(void)
{
    fprintf(stderr,
      "usage: kgzip [-cv] [-f format] [-l loader] [-o output] file\n");
    exit(1);
}
