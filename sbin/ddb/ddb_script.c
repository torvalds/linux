/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007 Robert N. M. Watson
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

#include <sys/types.h>
#include <sys/sysctl.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>

#include "ddb.h"

/*
 * These commands manage DDB(4) scripts from user space.  For better or worse,
 * the setting and unsetting of scripts is only poorly represented using
 * sysctl(8), and this interface provides a more user-friendly way to
 * accomplish this management, wrapped around lower-level sysctls.  For
 * completeness, listing of scripts is also included.
 */

#define	SYSCTL_SCRIPT	"debug.ddb.scripting.script"
#define	SYSCTL_SCRIPTS	"debug.ddb.scripting.scripts"
#define	SYSCTL_UNSCRIPT	"debug.ddb.scripting.unscript"

/*
 * Print all scripts (scriptname==NULL) or a specific script.
 */
static void
ddb_list_scripts(const char *scriptname)
{
	char *buffer, *line, *nextline;
	char *line_script, *line_scriptname;
	size_t buflen, len;
	int ret;

repeat:
	if (sysctlbyname(SYSCTL_SCRIPTS, NULL, &buflen, NULL, 0) < 0)
		err(EX_OSERR, "sysctl: %s", SYSCTL_SCRIPTS);
	if (buflen == 0)
		return;
	buffer = malloc(buflen);
	if (buffer == NULL)
		err(EX_OSERR, "malloc");
	bzero(buffer, buflen);
	len = buflen;
	ret = sysctlbyname(SYSCTL_SCRIPTS, buffer, &len, NULL, 0);
	if (ret < 0 && errno != ENOMEM)
		err(EX_OSERR, "sysctl: %s", SYSCTL_SCRIPTS);
	if (ret < 0) {
		free(buffer);
		goto repeat;
	}

	/*
	 * We nul'd the buffer before calling sysctl(), so at worst empty.
	 *
	 * If a specific script hasn't been requested, print it all.
	 */
	if (scriptname == NULL) {
		printf("%s", buffer);
		free(buffer);
		return;
	}

	/*
	 * If a specific script has been requested, we have to parse the
	 * string to find it.
	 */
	nextline = buffer;
	while ((line = strsep(&nextline, "\n")) != NULL) {
		line_script = line;
		line_scriptname = strsep(&line_script, "=");
		if (line_script == NULL)
			continue;
		if (strcmp(scriptname, line_scriptname) != 0)
			continue;
		printf("%s\n", line_script);
		break;
	}
	if (line == NULL) {
		errno = ENOENT;
		err(EX_DATAERR, "%s", scriptname);
	}
	free(buffer);
}

/*
 * "ddb script" can be used to either print or set a script.
 */
void
ddb_script(int argc, char *argv[])
{

	if (argc != 2)
		usage();
	argv++;
	argc--;
	if (strchr(argv[0], '=') != 0) {
		if (sysctlbyname(SYSCTL_SCRIPT, NULL, NULL, argv[0],
		    strlen(argv[0]) + 1) < 0)
			err(EX_OSERR, "sysctl: %s", SYSCTL_SCRIPTS);
	} else
		ddb_list_scripts(argv[0]);
}

void
ddb_scripts(int argc, char *argv[] __unused)
{

	if (argc != 1)
		usage();
	ddb_list_scripts(NULL);
}

void
ddb_unscript(int argc, char *argv[])
{
	int ret;

	if (argc != 2)
		usage();
	argv++;
	argc--;
	ret = sysctlbyname(SYSCTL_UNSCRIPT, NULL, NULL, argv[0],
	    strlen(argv[0]) + 1);
	if (ret < 0 && errno == EINVAL) {
		errno = ENOENT;
		err(EX_DATAERR, "sysctl: %s", argv[0]);
	} else if (ret < 0)
		err(EX_OSERR, "sysctl: %s", SYSCTL_UNSCRIPT);
}
