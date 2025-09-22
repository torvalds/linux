/* $OpenBSD: keynote-main.c,v 1.10 2004/06/25 05:06:49 msf Exp $ */
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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "header.h"

void	mainusage(void);

void
mainusage(void)
{
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "\tkeygen ...\n");
    fprintf(stderr, "\tsign ...\n");
    fprintf(stderr, "\tsigver ...\n");
    fprintf(stderr, "\tverify ...\n");
    fprintf(stderr, "Issue one of the commands by itself to get more help, "
		    "e.g., keynote sign\n");
}

int
main(int argc, char *argv[])
{
    if (argc < 2)
    {
	mainusage();
	exit(1);
    }

    if (!strcmp(argv[1], "sign"))
      keynote_sign(argc - 1, argv + 1);
    else
      if (!strcmp(argv[1], "verify"))
	keynote_verify(argc - 1, argv + 1);
      else
	if (!strcmp(argv[1], "sigver"))
	  keynote_sigver(argc - 1, argv + 1);
	else
	  if (!strcmp(argv[1], "keygen"))
	    keynote_keygen(argc - 1, argv + 1);

    mainusage();
    exit(1);
}
