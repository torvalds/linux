/* $OpenBSD: keynote-sigver.c,v 1.18 2021/10/24 21:24:20 deraadt Exp $ */
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
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "header.h"
#include "keynote.h"

void	sigverusage(void);

void
sigverusage(void)
{
    fprintf(stderr, "Arguments:\n");
    fprintf(stderr, "\t<AssertionFile>\n");
}

void
keynote_sigver(int argc, char *argv[])
{
    char *buf, **assertlist;
    int fd, i, n, j;
    struct stat sb;

    if (argc != 2)
    {
	sigverusage();
	exit(0);
    }

    /* Open and read assertion file */
    fd = open(argv[1], O_RDONLY);
    if (fd == -1)
    {
	perror(argv[1]);
	exit(1);
    }

    if (fstat(fd, &sb) == -1)
    {
	perror("fstat()");
	exit(1);
    }

    if (sb.st_size == 0) /* Paranoid */
    {
	fprintf(stderr, "Illegal assertion-file size 0\n");
	exit(1);
    }

    buf = calloc(sb.st_size + 1, sizeof(char));
    if (buf == NULL)
    {
	perror("calloc()");
	exit(1);
    }

    if (read(fd, buf, sb.st_size) == -1)
    {
	perror("read()");
	exit(1);
    }

    close(fd);

    assertlist = kn_read_asserts(buf, sb.st_size, &n);
    if (assertlist == NULL)
    {
      	fprintf(stderr, "Out of memory while allocating memory for "
		"assertions.\n");
	exit(1);
    }

    if (n == 0)
    {
	fprintf(stderr, "No assertions found in %s.\n", argv[1]);
	free(assertlist);
	exit(1);
    }

    free(buf);

    for (j = 0; j < n; j++)
    {
	i = kn_verify_assertion(assertlist[j], strlen(assertlist[j]));
	if (i == -1)
	{
	    switch (keynote_errno)
	    {
		case ERROR_MEMORY:
		    fprintf(stderr,
			    "Out of memory while parsing assertion %d.\n", j);
		    break;

		case ERROR_SYNTAX:
		    fprintf(stderr,
			    "Syntax error while parsing assertion %d.\n", j);
		    break;

		default:
		    fprintf(stderr,
			    "Unknown error while parsing assertion %d.\n", j);
	    }
	}
	else
	{
	    if (i == SIGRESULT_TRUE)
	      fprintf(stdout, "Signature on assertion %d verified.\n", j);
	    else
	    {
		if (keynote_errno != 0)
		  fprintf(stdout,
			  "Signature on assertion %d could not be verified "
			  "(keynote_errno = %d).\n", j, keynote_errno);
		else
		  fprintf(stdout,
			  "Signature on assertion %d did not verify!\n", j);
	    }
	}

	free(assertlist[j]);
    }

    free(assertlist);

    exit(0);
}
