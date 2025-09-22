/* $OpenBSD: keynote-sign.c,v 1.20 2021/10/24 21:24:20 deraadt Exp $ */
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
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include "header.h"
#include "keynote.h"

void	signusage(void);

void
signusage(void)
{
    fprintf(stderr, "Arguments:\n");
    fprintf(stderr, "\t[-v] <AlgorithmName> <AssertionFile> "
	    "<PrivateKeyFile> [<print-offset>] [<print-length>]\n");
}

void
keynote_sign(int argc, char *argv[])
{
    int begin = SIG_PRINT_OFFSET, prlen = SIG_PRINT_LENGTH;
    char *buf, *buf2, *sig, *algname;
    int fd, flg = 0, buflen;
    struct stat sb;

    if ((argc != 4) &&
	(argc != 5) &&
	(argc != 6) &&
	(argc != 7))
    {
	signusage();
	exit(1);
    }

    if (!strcmp("-v", argv[1]))
      flg = 1;

    if (argc > 4 + flg)
    {
        begin = atoi(argv[4 + flg]);
        if (begin <= -1)
        {
            fprintf(stderr, "Erroneous value for print-offset parameter.\n");
            exit(1);
        }
    }
        
    if (argc > 5 + flg)
    {
        prlen = atoi(argv[5 + flg]);
        if (prlen <= 0)
        {
            fprintf(stderr, "Erroneous value for print-length parameter.\n");
            exit(1);
        }
    }

    /* Fix algorithm name */
    if (argv[1 + flg][strlen(argv[1 + flg]) - 1] != ':')
    {
	int len = strlen(argv[1 + flg]) + 2;
        fprintf(stderr, "Algorithm name [%s] should be terminated with a "
		"colon, fixing.\n", argv[1 + flg]);
	algname = calloc(len, sizeof(char));
	if (algname == NULL)
	{
	    perror("calloc()");
	    exit(1);
	}

	strlcpy(algname, argv[1 + flg], len);
	algname[strlen(algname)] = ':';
    }
    else
	algname = argv[1 + flg];

    /* Read assertion */
    fd = open(argv[2 + flg], O_RDONLY);
    if (fd == -1)
    {
	perror(argv[2 + flg]);
	exit(1);
    }

    if (fstat(fd, &sb) == -1)
    {
	perror("fstat()");
	exit(1);
    }

    if (sb.st_size == 0) /* Paranoid */
    {
	fprintf(stderr, "Error: zero-sized assertion-file.\n");
	exit(1);
    }

    buflen = sb.st_size + 1;
    buf = calloc(buflen, sizeof(char));
    if (buf == NULL)
    {
	perror("calloc()");
	exit(1);
    }

    if (read(fd, buf, buflen - 1) == -1)
    {
	perror("read()");
	exit(1);
    }

    close(fd);

    /* Read private key file */
    fd = open(argv[3 + flg], O_RDONLY);
    if (fd == -1)
    {
	perror(argv[3 + flg]);
	exit(1);
    }

    if (fstat(fd, &sb) == -1)
    {
	perror("fstat()");
	exit(1);
    }

    if (sb.st_size == 0) /* Paranoid */
    {
	fprintf(stderr, "Illegal key-file size 0\n");
	exit(1);
    }

    buf2 = calloc(sb.st_size + 1, sizeof(char));
    if (buf2 == NULL)
    {
	perror("calloc()");
	exit(1);
    }

    if (read(fd, buf2, sb.st_size) == -1)
    {
	perror("read()");
	exit(1);
    }

    close(fd);

    sig = kn_sign_assertion(buf, buflen, buf2, algname, flg);

    /* Free buffers */
    free(buf);
    free(buf2);

    if (sig == NULL)
    {
	switch (keynote_errno)
	{
	    case ERROR_MEMORY:
		fprintf(stderr, "Out of memory while creating signature.\n");
		break;

	    case ERROR_SYNTAX:
		fprintf(stderr, "Bad assertion or algorithm format, or "
			"unsupported algorithm while creating signature.\n");
		break;

	    default:
		fprintf(stderr, "Unknown error while creating signature.\n");
	}

	exit(1);
    }

    /* Print signature string */
    print_key(stdout, "", sig, begin, prlen);

    free(sig);   /* Just a reminder that the result is malloc'ed */

    exit(0);
}
