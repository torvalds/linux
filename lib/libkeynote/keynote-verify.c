/* $OpenBSD: keynote-verify.c,v 1.20 2021/10/24 21:24:20 deraadt Exp $ */
/*
 * The author of this code is Angelos D. Keromytis (angelos@dsl.cis.upenn.edu)
 *
 * This code was written by Angelos D. Keromytis in Philadelphia, PA, USA,
 * in April-May 1998
 *
 * Copyright (C) 1998, 1999 by Angelos D. Keromytis.
 *	
 * Permission to use, copy, and modify this software with or without fee
 * is hereby granted, provided that this entire notice is included in
 * all copies of any software which is or includes a copy or
 * modification of this software. 
 *
 * THIS SOFTWARE IS BEING PROVIDED "AS IS", WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTY. IN PARTICULAR, THE AUTHORS MAKES NO
 * REPRESENTATION OR WARRANTY OF ANY KIND CONCERNING THE
 * MERCHANTABILITY OF THIS SOFTWARE OR ITS FITNESS FOR ANY PARTICULAR
 * PURPOSE.
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <ctype.h>
#include <fcntl.h>
#include <getopt.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "header.h"
#include "keynote.h"

int sessid;

void	verifyusage(void);

void
verifyusage(void)
{
    fprintf(stderr, "Arguments:\n");
    fprintf(stderr, "\t-h:             This message\n");
    fprintf(stderr,
	    "\t-r <valuelist>: Comma separated, ordered return-value list\n");
    fprintf(stderr, "\t-e <filename>:  Environment settings\n");
    fprintf(stderr, "\t-l <filename>:  Trusted (local) assertion\n");
    fprintf(stderr, "\t-k <filename>:  File containing key\n");
    fprintf(stderr, "Followed by a list of:\n");
    fprintf(stderr, "\t<filename>:     Non-local assertion\n");
}

void
keynote_verify(int argc, char *argv[])
{
    int fd, i, ch, se = 0, cl = 8192, sk = 0, sl = 0, p, ac = argc;
    char *buf, **av = argv, **retv, **foov, *ptr;
    int numretv = 16, numret = 0, sn;
    struct stat sb;

    if (argc == 1)
    {
	verifyusage();
	exit(1);
    }

    if ((buf = calloc(cl, sizeof(char))) == NULL)
    {
	perror("calloc()");
	exit(1);
    }

    if ((retv = calloc(numretv, sizeof(char *))) == NULL)
    {
	perror("calloc()");
	exit(1);
    }

    /* "ac" and "av" are used for stress-testing, ignore otherwise */
    argv = av;
    argc = ac;
    sn = 0;
    opterr = 0;

    sessid = kn_init();
    if (sessid == -1)
    {
	fprintf(stderr, "kn_init() failed (errno %d).\n", keynote_errno);
	exit(keynote_errno);
    }
    
    while ((ch = getopt(argc, argv, "hqistl:e:k:r:")) != -1)
    {
	switch (ch) 
	{
	    case 'e':
		if (read_environment(optarg) == -1)
	 	  exit(1);
		se = 1;
		break;

	    case 'k':
		sk = 1;

		if ((fd = open(optarg, O_RDONLY)) == -1)
		{
		    perror(optarg);
		    exit(1);
		}

		if (fstat(fd, &sb) == -1)
		{
		    perror("fstat()");
		    exit(1);
		}

		if (sb.st_size > cl - 1)
		{
		    free(buf);
		    cl = sb.st_size + 1;
		    buf = calloc(cl, sizeof(char));
		    if (buf == NULL)
		    {
			perror("calloc()");
			exit(1);
		    }
		}

		i = read(fd, buf, sb.st_size);
		if (i == -1)
		{
		    perror("read()");
		    exit(1);
		}

		close(fd);

		parse_key(buf);
		switch (keynote_errno)
		{
		    case 0:    /* No errors */
			break;

		    case ERROR_SYNTAX:
			fprintf(stderr, "Syntax error adding authorizer "
				"%s\n", optarg);
			exit(1);

		    case ERROR_MEMORY:
			perror("Out of memory.\n");
			exit(1);

		    default:
			fprintf(stderr, "Unknown error (%d).\n",
				keynote_errno);
		}

		break;

	    case 'h':
		verifyusage();
		exit(0);

	    case 'r':
		if (sn != 0)
		{
		    fprintf(stderr,
			    "Do not define two sets of return values.\n");
		    exit(1);
		}

		sn = 1;

		for (numret = 0;
		     (ptr = strchr(optarg, ',')) != NULL;
		     numret++)
		{
		    /* Running out of memory */
		    if (numret > numretv - 3)
		    {
			numretv *= 2;
			foov = calloc(numretv, sizeof(char **));
			if (foov == NULL)
			{
			    /* 
			     * If this were a real program, we 'd be freeing
			     * retv here. Since we're exiting, we can be a
			     * little sloppy.
			     */
			    perror("calloc()");
			    exit(1);
			}

			memcpy(foov, retv, numretv * sizeof(char **));
			free(retv);
			retv = foov;
		    }

		    retv[numret] = calloc((ptr - optarg) + 1,
						       sizeof(char));
		    if (retv[numret] == NULL)
		    {
			/* Comment from above applies here as well */
			perror("calloc()");
			exit(1);
		    }

		    /* Copy */
		    memcpy(retv[numret], optarg, ptr - optarg);
		    optarg = ptr + 1;
		}

		/* Last component */
		retv[numret] = strdup(optarg);
		if (retv[numret] == NULL)
		{
		    perror("calloc()");
		    exit(1);
		}

		numret++;
		break;

	    case 'l':
		if ((fd = open(optarg, O_RDONLY)) == -1)
		{
		    perror(optarg);
		    exit(1);
		}

		if (fstat(fd, &sb) == -1)
		{
		    perror("fstat()");
		    exit(1);
		}

		if (sb.st_size > cl - 1)
		{
		    free(buf);
		    cl = sb.st_size + 1;
		    buf = calloc(cl, sizeof(char));
		    if (buf == NULL)
		    {
			perror("calloc()");
			exit(1);
		    }
		}

		i = read(fd, buf, sb.st_size);
		if (i == -1)
		{
		    perror("read()");
		    exit(1);
		}

		close(fd);
		p = kn_add_assertion(sessid, buf, i, ASSERT_FLAG_LOCAL);
		if (p == -1)
		{
		    fprintf(stderr,
			    "Error for assertion in file <%s>, errno %d.\n",
			   optarg, keynote_errno);
		    keynote_errno = 0;
		}

		memset(buf, 0, sb.st_size);
		sl = 1;
		break;

	    case '?':
	    default:
		verifyusage();
		exit(1);
	}
    }

    argc -= optind;
    argv += optind;
    optind = 1;

    if (sn == 0)
    {
	fprintf(stderr,
		"Should set return values before evaluations begin.\n");
	exit(1);
    }

    if (se == 0)
    {
	fprintf(stderr, "Should set environment before evaluations begin.\n");
	exit(1);
    }

    if (sk == 0)
    {
	fprintf(stderr, "Should specify at least one action authorizer.\n");
	exit(1);
    }

    if (sl == 0)
    {
	fprintf(stderr,
		"Should specify at least one trusted assertion (POLICY).\n");
	exit(1);
    }

    while (argc--)
    {
	if ((fd = open(argv[argc], O_RDONLY)) == -1)
	{
	    perror(argv[argc]);
	    exit(1);
	}

	if (fstat(fd, &sb) == -1)
	{
	    perror("fstat()");
	    exit(1);
	}

	if (sb.st_size > cl - 1)
	{
	    free(buf);
	    cl = sb.st_size + 1;
	    buf = calloc(cl, sizeof(char));
	    if (buf == NULL)
	    {
		perror("calloc()");
		exit(1);
	    }
	}

	i = read(fd, buf, sb.st_size);
	if (i == -1)
	{
	    perror("read()");
	    exit(1);
	}

	close(fd);
	p = kn_add_assertion(sessid, buf, i, 0);
	if (p == -1)
	{
	    fprintf(stderr, "Error for assertion in file <%s>, errno %d.\n",
		    argv[argc], keynote_errno);
	    keynote_errno = 0;
	}

	memset(buf, 0, sb.st_size);
    }

    p = kn_do_query(sessid, retv, numret); /* Evaluation time */

    printf("Query result = ");

    switch (keynote_errno)
    {
	case ERROR_MEMORY:
	    printf("<out of memory>\n");
	    exit(1);

	case ERROR_SYNTAX:
	    printf("<uninitialized authorizers or all POLICY "
		   "assertions are malformed!>\n");
	    exit(1);

	case ERROR_NOTFOUND:
	    printf("<session or other information not found!>\n");
	    exit(1);

	case 0:	/* No errors */
	    break;

	default:
	    printf("<should never happen (%d)!>\n", keynote_errno);
	    exit(1);
    }

    printf("%s\n", retv[p]);

    keynote_errno = 0;

    while ((i = kn_get_failed(sessid, KEYNOTE_ERROR_MEMORY, 0)) != -1)
    {
        printf("Failed assertion %d due to memory error.\n", i);
        kn_remove_assertion(sessid, i);
    }

    while ((i = kn_get_failed(sessid, KEYNOTE_ERROR_SYNTAX, 0)) != -1)
    {
        printf("Failed assertion %d due to syntax or semantic error.\n", i);
        kn_remove_assertion(sessid, i);
    }

    while ((i = kn_get_failed(sessid, KEYNOTE_ERROR_SIGNATURE, 0)) != -1)
    {
        printf("Failed assertion %d due to signature verification failure.\n",
	       i);
        kn_remove_assertion(sessid, i);
    }

    while ((i = kn_get_failed(sessid, KEYNOTE_ERROR_ANY, 0)) != -1)
    {
        printf("Failed assertion %d due to unspecified error.\n", i);
        kn_remove_assertion(sessid, i);
    }

    kn_close(sessid);

    /* This is a reminder that return values are not free'ed by KeyNote */
    for (sn = 0; sn < numret; sn++)
      free(retv[sn]);
    free(retv);
    retv = NULL;

    exit(0);
}
