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
 */
/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Copyright (c) 2013, 2016 by Delphix. All rights reserved.
 */

/*
 * Common name validation routines for ZFS.  These routines are shared by the
 * userland code as well as the ioctl() layer to ensure that we don't
 * inadvertently expose a hole through direct ioctl()s that never gets tested.
 * In userland, however, we want significantly more information about _why_ the
 * name is invalid.  In the kernel, we only care whether it's valid or not.
 * Each routine therefore takes a 'namecheck_err_t' which describes exactly why
 * the name failed to validate.
 */

#if defined(_KERNEL)
#include <sys/systm.h>
#else
#include <string.h>
#endif

#include <sys/dsl_dir.h>
#include <sys/param.h>
#include <sys/nvpair.h>
#include "zfs_namecheck.h"
#include "zfs_deleg.h"

/*
 * Deeply nested datasets can overflow the stack, so we put a limit
 * in the amount of nesting a path can have. zfs_max_dataset_nesting
 * can be tuned temporarily to fix existing datasets that exceed our
 * predefined limit.
 */
int zfs_max_dataset_nesting = 50;

static int
valid_char(char c)
{
	return ((c >= 'a' && c <= 'z') ||
	    (c >= 'A' && c <= 'Z') ||
	    (c >= '0' && c <= '9') ||
	    c == '-' || c == '_' || c == '.' || c == ':' || c == ' ');
}

/*
 * Looks at a path and returns its level of nesting (depth).
 */
int
get_dataset_depth(const char *path)
{
	const char *loc = path;
	int nesting = 0;

	/*
	 * Keep track of nesting until you hit the end of the
	 * path or found the snapshot/bookmark seperator.
	 */
	for (int i = 0; loc[i] != '\0' &&
	    loc[i] != '@' &&
	    loc[i] != '#'; i++) {
		if (loc[i] == '/')
			nesting++;
	}

	return (nesting);
}

/*
 * Snapshot names must be made up of alphanumeric characters plus the following
 * characters:
 *
 *	[-_.: ]
 *
 * Returns 0 on success, -1 on error.
 */
int
zfs_component_namecheck(const char *path, namecheck_err_t *why, char *what)
{
	const char *loc;

	if (strlen(path) >= ZFS_MAX_DATASET_NAME_LEN) {
		if (why)
			*why = NAME_ERR_TOOLONG;
		return (-1);
	}

	if (path[0] == '\0') {
		if (why)
			*why = NAME_ERR_EMPTY_COMPONENT;
		return (-1);
	}

	for (loc = path; *loc; loc++) {
		if (!valid_char(*loc)) {
			if (why) {
				*why = NAME_ERR_INVALCHAR;
				*what = *loc;
			}
			return (-1);
		}
	}
	return (0);
}


/*
 * Permissions set name must start with the letter '@' followed by the
 * same character restrictions as snapshot names, except that the name
 * cannot exceed 64 characters.
 *
 * Returns 0 on success, -1 on error.
 */
int
permset_namecheck(const char *path, namecheck_err_t *why, char *what)
{
	if (strlen(path) >= ZFS_PERMSET_MAXLEN) {
		if (why)
			*why = NAME_ERR_TOOLONG;
		return (-1);
	}

	if (path[0] != '@') {
		if (why) {
			*why = NAME_ERR_NO_AT;
			*what = path[0];
		}
		return (-1);
	}

	return (zfs_component_namecheck(&path[1], why, what));
}

/*
 * Dataset paths should not be deeper than zfs_max_dataset_nesting
 * in terms of nesting.
 *
 * Returns 0 on success, -1 on error.
 */
int
dataset_nestcheck(const char *path)
{
	return ((get_dataset_depth(path) < zfs_max_dataset_nesting) ? 0 : -1);
}

/*
 * Entity names must be of the following form:
 *
 *	[component/]*[component][(@|#)component]?
 *
 * Where each component is made up of alphanumeric characters plus the following
 * characters:
 *
 *	[-_.:%]
 *
 * We allow '%' here as we use that character internally to create unique
 * names for temporary clones (for online recv).
 *
 * Returns 0 on success, -1 on error.
 */
int
entity_namecheck(const char *path, namecheck_err_t *why, char *what)
{
	const char *end;

	/*
	 * Make sure the name is not too long.
	 */
	if (strlen(path) >= ZFS_MAX_DATASET_NAME_LEN) {
		if (why)
			*why = NAME_ERR_TOOLONG;
		return (-1);
	}

	/* Explicitly check for a leading slash.  */
	if (path[0] == '/') {
		if (why)
			*why = NAME_ERR_LEADING_SLASH;
		return (-1);
	}

	if (path[0] == '\0') {
		if (why)
			*why = NAME_ERR_EMPTY_COMPONENT;
		return (-1);
	}

	const char *start = path;
	boolean_t found_delim = B_FALSE;
	for (;;) {
		/* Find the end of this component */
		end = start;
		while (*end != '/' && *end != '@' && *end != '#' &&
		    *end != '\0')
			end++;

		if (*end == '\0' && end[-1] == '/') {
			/* trailing slashes are not allowed */
			if (why)
				*why = NAME_ERR_TRAILING_SLASH;
			return (-1);
		}

		/* Validate the contents of this component */
		for (const char *loc = start; loc != end; loc++) {
			if (!valid_char(*loc) && *loc != '%') {
				if (why) {
					*why = NAME_ERR_INVALCHAR;
					*what = *loc;
				}
				return (-1);
			}
		}

		/* Snapshot or bookmark delimiter found */
		if (*end == '@' || *end == '#') {
			/* Multiple delimiters are not allowed */
			if (found_delim != 0) {
				if (why)
					*why = NAME_ERR_MULTIPLE_DELIMITERS;
				return (-1);
			}

			found_delim = B_TRUE;
		}

		/* Zero-length components are not allowed */
		if (start == end) {
			if (why)
				*why = NAME_ERR_EMPTY_COMPONENT;
			return (-1);
		}

		/* If we've reached the end of the string, we're OK */
		if (*end == '\0')
			return (0);

		/*
		 * If there is a '/' in a snapshot or bookmark name
		 * then report an error
		 */
		if (*end == '/' && found_delim != 0) {
			if (why)
				*why = NAME_ERR_TRAILING_SLASH;
			return (-1);
		}

		/* Update to the next component */
		start = end + 1;
	}
}

/*
 * Dataset is any entity, except bookmark
 */
int
dataset_namecheck(const char *path, namecheck_err_t *why, char *what)
{
	int ret = entity_namecheck(path, why, what);

	if (ret == 0 && strchr(path, '#') != NULL) {
		if (why != NULL) {
			*why = NAME_ERR_INVALCHAR;
			*what = '#';
		}
		return (-1);
	}

	return (ret);
}

/*
 * mountpoint names must be of the following form:
 *
 *	/[component][/]*[component][/]
 *
 * Returns 0 on success, -1 on error.
 */
int
mountpoint_namecheck(const char *path, namecheck_err_t *why)
{
	const char *start, *end;

	/*
	 * Make sure none of the mountpoint component names are too long.
	 * If a component name is too long then the mkdir of the mountpoint
	 * will fail but then the mountpoint property will be set to a value
	 * that can never be mounted.  Better to fail before setting the prop.
	 * Extra slashes are OK, they will be tossed by the mountpoint mkdir.
	 */

	if (path == NULL || *path != '/') {
		if (why)
			*why = NAME_ERR_LEADING_SLASH;
		return (-1);
	}

	/* Skip leading slash  */
	start = &path[1];
	do {
		end = start;
		while (*end != '/' && *end != '\0')
			end++;

		if (end - start >= ZFS_MAX_DATASET_NAME_LEN) {
			if (why)
				*why = NAME_ERR_TOOLONG;
			return (-1);
		}
		start = end + 1;

	} while (*end != '\0');

	return (0);
}

/*
 * For pool names, we have the same set of valid characters as described in
 * dataset names, with the additional restriction that the pool name must begin
 * with a letter.  The pool names 'raidz' and 'mirror' are also reserved names
 * that cannot be used.
 *
 * Returns 0 on success, -1 on error.
 */
int
pool_namecheck(const char *pool, namecheck_err_t *why, char *what)
{
	const char *c;

	/*
	 * Make sure the name is not too long.
	 * If we're creating a pool with version >= SPA_VERSION_DSL_SCRUB (v11)
	 * we need to account for additional space needed by the origin ds which
	 * will also be snapshotted: "poolname"+"/"+"$ORIGIN"+"@"+"$ORIGIN".
	 * Play it safe and enforce this limit even if the pool version is < 11
	 * so it can be upgraded without issues.
	 */
	if (strlen(pool) >= (ZFS_MAX_DATASET_NAME_LEN - 2 -
	    strlen(ORIGIN_DIR_NAME) * 2)) {
		if (why)
			*why = NAME_ERR_TOOLONG;
		return (-1);
	}

	c = pool;
	while (*c != '\0') {
		if (!valid_char(*c)) {
			if (why) {
				*why = NAME_ERR_INVALCHAR;
				*what = *c;
			}
			return (-1);
		}
		c++;
	}

	if (!(*pool >= 'a' && *pool <= 'z') &&
	    !(*pool >= 'A' && *pool <= 'Z')) {
		if (why)
			*why = NAME_ERR_NOLETTER;
		return (-1);
	}

	if (strcmp(pool, "mirror") == 0 || strcmp(pool, "raidz") == 0) {
		if (why)
			*why = NAME_ERR_RESERVED;
		return (-1);
	}

	if (pool[0] == 'c' && (pool[1] >= '0' && pool[1] <= '9')) {
		if (why)
			*why = NAME_ERR_DISKLIKE;
		return (-1);
	}

	return (0);
}
