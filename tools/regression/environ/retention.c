/*-
 * Copyright (c) 2007 Sean C. Farley <scf@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");


extern char **environ;
const char *envName = "FOOBAR";
const char *envValSmall = "Hi";
const char *envValLarge = "Hi, again";
const char *envValAny = "Any value";


int
main(int argc, char **argv)
{
	const char *env1 = NULL;
	const char *env2 = NULL;
	const char *env3 = NULL;
	const char *env4 = NULL;
	const char *env5 = NULL;
	int testNdx;

	/* Clean slate. */
	environ = NULL;
	testNdx = 0;

	/* Initial value of variable. */
	if (getenv(envName) != NULL)
		printf("not ");
	printf("ok %d - getenv(\"%s\")\n", ++testNdx, envName);

	/* Set value of variable to smaller value and get value. */
	if ((setenv(envName, envValSmall, 1) != 0) ||
	    ((env1 = getenv(envName)) == NULL) ||
	    (strcmp(env1, envValSmall) != 0))
		printf("not ");
	printf("ok %d - setenv(\"%s\", \"%s\", 1)\n", ++testNdx, envName,
	    envValSmall);

	/* Unset variable. */
	if ((unsetenv(envName) == -1) || ((env2 = getenv(envName)) != NULL))
		printf("not ");
	printf("ok %d - unsetenv(\"%s\")\n", ++testNdx, envName);

	/* Set variable to bigger value and get value. */
	if ((setenv(envName, envValLarge, 1) != 0) ||
	    ((env3 = getenv(envName)) == NULL) ||
	    (strcmp(env3, envValLarge) != 0))
		printf("not ");
	printf("ok %d - setenv(\"%s\", \"%s\", 1)\n", ++testNdx, envName,
	    envValLarge);

	/* Set variable to smaller value and get value. */
	if ((setenv(envName, envValSmall, 1) != 0) ||
	    ((env4 = getenv(envName)) == NULL) ||
	    (strcmp(env4, envValSmall) != 0))
		printf("not ");
	printf("ok %d - setenv(\"%s\", \"%s\", 1)\n", ++testNdx, envName,
	    envValSmall);

	/* Set variable to any value without overwrite and get value. */
	if ((setenv(envName, envValAny, 0) != 0) ||
	    ((env5 = getenv(envName)) == NULL) ||
	    (strcmp(env5, envValAny) == 0))
		printf("not ");
	printf("ok %d - setenv(\"%s\", \"%s\", 0)\n", ++testNdx, envName,
	    envValAny);

	/*
	 * Verify FreeBSD-ism about allowing a program to keep old pointers without
	 * risk of segfaulting.
	 */
	if ((strcmp(env1, envValSmall) != 0) ||
	    (strcmp(env3, envValSmall) != 0) ||
	    (strcmp(env4, envValSmall) != 0))
		printf("not ");
	printf("ok %d - old variables point to valid memory\n", ++testNdx);

	exit(EXIT_SUCCESS);
}
