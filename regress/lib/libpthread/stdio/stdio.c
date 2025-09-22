/*	$OpenBSD: stdio.c,v 1.3 2004/02/27 19:58:08 deraadt Exp $	*/
/*
 * Copyright (c) 1993, 1994, 1995, 1996 by Chris Provenzano and contributors, 
 * proven@mit.edu All rights reserved.
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
 *	This product includes software developed by Chris Provenzano,
 *	the University of California, Berkeley, and contributors.
 * 4. Neither the name of Chris Provenzano, the University, nor the names of
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY CHRIS PROVENZANO AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL CHRIS PROVENZANO, THE REGENTS OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */ 

#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "test.h"

char * base_name = "stdio.c";
char * dir_name = SRCDIR;
char * fullname;

/* Test fopen()/ftell()/getc() */
static void
test_1(void)
{
	struct stat statbuf;
	FILE * fp;
	int i;

	CHECKe(stat(fullname, &statbuf));

	CHECKn((fp = fopen(fullname, "r")));

	/* Get the entire file */
	while ((i = getc(fp)) != EOF)
		;

	ASSERT(ftell(fp) == statbuf.st_size);

	CHECKe(fclose(fp));
}

/* Test fopen()/fclose() */
static void
test_2(void)
{
	FILE *fp1, *fp2;

	CHECKn(fp1 = fopen(fullname, "r"));
	CHECKe(fclose(fp1));

	CHECKn(fp2 = fopen(fullname, "r"));
	CHECKe(fclose(fp2));

	ASSERT(fp1 == fp2);
}

/* Test sscanf()/sprintf() */
static void
test_3(void)
{
	char * str = "10 4.53";
	char buf[64];
	double d;
	int    i;

	ASSERT(sscanf(str, "%d %lf", &i, &d) == 2);

	/* Should have a check */
	snprintf(buf, sizeof buf, "%d %2.2f", i, d);
	ASSERT(strcmp(buf, str) == 0);
}

int
main(int argc, char *argv[])
{

	int len = strlen (dir_name) + strlen (base_name) + 2;

	CHECKn(fullname = malloc (len));
	snprintf(fullname, len, "%s/%s", dir_name, base_name);

	test_1();
	test_2();
	test_3();

	SUCCEED;
}
