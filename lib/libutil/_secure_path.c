/*-
 * Based on code copyright (c) 1995,1997 by
 * Berkeley Software Design, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, is permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice immediately at the beginning of the file, without modification,
 *    this list of conditions, and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. This work was done expressly for inclusion into FreeBSD.  Other use
 *    is permitted provided this notation is included.
 * 4. Absolutely no warranty of function or purpose is made by the authors.
 * 5. Modifications may be freely made to this file providing the above
 *    conditions are met.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <libutil.h>
#include <stddef.h>
#include <syslog.h>

/*
 * Check for common security problems on a given path
 * It must be:
 * 1. A regular file, and exists
 * 2. Owned and writable only by root (or given owner)
 * 3. Group ownership is given group or is non-group writable
 *
 * Returns:	-2 if file does not exist,
 *		-1 if security test failure
 *		0  otherwise
 */

int
_secure_path(const char *path, uid_t uid, gid_t gid)
{
    int		r = -1;
    struct stat	sb;
    const char	*msg = NULL;

    if (lstat(path, &sb) < 0) {
	if (errno == ENOENT) /* special case */
	    r = -2;  /* if it is just missing, skip the log entry */
	else
	    msg = "%s: cannot stat %s: %m";
    }
    else if (!S_ISREG(sb.st_mode))
    	msg = "%s: %s is not a regular file";
    else if (sb.st_mode & S_IWOTH)
    	msg = "%s: %s is world writable";
    else if ((int)uid != -1 && sb.st_uid != uid && sb.st_uid != 0) {
    	if (uid == 0)
    		msg = "%s: %s is not owned by root";
    	else
    		msg = "%s: %s is not owned by uid %d";
    } else if ((int)gid != -1 && sb.st_gid != gid && (sb.st_mode & S_IWGRP))
    	msg = "%s: %s is group writeable by non-authorised groups";
    else
    	r = 0;
    if (msg != NULL)
	syslog(LOG_ERR, msg, "_secure_path", path, uid);
    return r;
}
