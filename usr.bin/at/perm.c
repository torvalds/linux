/*-
 *  perm.c - check user permission for at(1)
 *
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 *  Copyright (C) 1994  Thomas Koenig
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the author(s) may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/* System Headers */

#include <sys/types.h>
#include <err.h>
#include <errno.h>
#include <pwd.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Local headers */

#include "at.h"
#include "perm.h"
#include "privs.h"

/* Macros */

#define MAXUSERID 10

/* Structures and unions */

/* Function declarations */

static int check_for_user(FILE *fp,const char *name);

/* Local functions */

static int check_for_user(FILE *fp,const char *name)
{
    char *buffer;
    size_t len;
    int found = 0;

    len = strlen(name);
    if ((buffer = malloc(len+2)) == NULL)
	errx(EXIT_FAILURE, "virtual memory exhausted");

    while(fgets(buffer, len+2, fp) != NULL)
    {
	if ((strncmp(name, buffer, len) == 0) &&
	    (buffer[len] == '\n'))
	{
	    found = 1;
	    break;
	}
    }
    fclose(fp);
    free(buffer);
    return found;
}
/* Global functions */
int check_permission(void)
{
    FILE *fp;
    uid_t uid = geteuid();
    struct passwd *pentry;

    if (uid==0)
	return 1;

    if ((pentry = getpwuid(uid)) == NULL)
	err(EXIT_FAILURE, "cannot access user database");

    PRIV_START

    fp=fopen(PERM_PATH "at.allow","r");

    PRIV_END

    if (fp != NULL)
    {
	return check_for_user(fp, pentry->pw_name);
    }
    else if (errno == ENOENT)
    {

	PRIV_START

	fp=fopen(PERM_PATH "at.deny", "r");

	PRIV_END

	if (fp != NULL)
	{
	    return !check_for_user(fp, pentry->pw_name);
	}
	else if (errno != ENOENT)
	    warn("at.deny");
    }
    else
	warn("at.allow");
    return 0;
}
