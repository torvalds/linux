/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 * $FreeBSD$
 */

/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"@(#)chg_usr_exec.c	1.3	07/05/25 SMI"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <pwd.h>

int
main(int argc, char *argv[])
{
	char *plogin = NULL;
	char cmds[BUFSIZ] = { 0 };
	char sep[] = " ";
	struct passwd *ppw = NULL;
	int i, len;

	if (argc < 3 || strlen(argv[1]) == 0) {
		(void) printf("\tUsage: %s <login> <commands> ...\n", argv[0]);
		return (1);
	}

	plogin = argv[1];
	len = 0;
	for (i = 2; i < argc; i++) {
		(void) snprintf(cmds+len, sizeof (cmds)-len,
		    "%s%s", argv[i], sep);
		len += strlen(argv[i]) + strlen(sep);
	}

	if ((ppw = getpwnam(plogin)) == NULL) {
		perror("getpwnam");
		return (errno);
	}
	if (setgid(ppw->pw_gid) != 0) {
		perror("setgid");
		return (errno);
	}
	if (setuid(ppw->pw_uid) != 0) {
		perror("setuid");
		return (errno);
	}

	if (execl("/bin/sh", "sh",  "-c", cmds, (char *)0) != 0) {
		perror("execl");
		return (errno);
	}

	return (0);
}
