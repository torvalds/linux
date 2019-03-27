/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2014 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Edward Tomasz Napierala under sponsorship
 * from the FreeBSD Foundation.
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
 *
 */

/*
 * All the "defined" stuff is for handling variables,
 * such as ${OSNAME}, in maps.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/linker.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/utsname.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <libutil.h>
#include <netdb.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"

static TAILQ_HEAD(, defined_value)	defined_values;

static const char *
defined_find(const char *name)
{
	struct defined_value *d;

	TAILQ_FOREACH(d, &defined_values, d_next) {
		if (strcmp(d->d_name, name) == 0)
			return (d->d_value);
	}

	return (NULL);
}

char *
defined_expand(const char *string)
{
	const char *value;
	char c, *expanded, *name;
	int i, ret, before_len = 0, name_off = 0, name_len = 0, after_off = 0;
	bool backslashed = false, bracketed = false;

	expanded = checked_strdup(string);

	for (i = 0; string[i] != '\0'; i++) {
		c = string[i];
		if (c == '\\' && backslashed == false) {
			backslashed = true;
			continue;
		}
		if (backslashed) {
			backslashed = false;
			continue;
		}
		backslashed = false;
		if (c != '$')
			continue;

		/*
		 * The 'before_len' variable contains the number
		 * of characters before the '$'.
		 */
		before_len = i;
		assert(i + 1 < (int)strlen(string));
		if (string[i + 1] == '{')
			bracketed = true;

		if (string[i + 1] == '\0') {
			log_warnx("truncated variable");
			return (NULL);
		}

		/*
		 * Skip '$'.
		 */
		i++;

		if (bracketed) {
			if (string[i + 1] == '\0') {
				log_warnx("truncated variable");
				return (NULL);
			}

			/*
			 * Skip '{'.
			 */
			i++;
		}

		/*
		 * The 'name_off' variable contains the number
		 * of characters before the variable name,
		 * including the "$" or "${".
		 */
		name_off = i;

		for (; string[i] != '\0'; i++) {
			c = string[i];
			/*
			 * XXX: Decide on the set of characters that can be
			 *	used in a variable name.
			 */
			if (isalnum(c) || c == '_')
				continue;

			/*
			 * End of variable name.
			 */
			if (bracketed) {
				if (c != '}')
					continue;

				/*
				 * The 'after_off' variable contains the number
				 * of characters before the rest of the string,
				 * i.e. after the variable name.
				 */
				after_off = i + 1;
				assert(i > 1);
				assert(i - 1 > name_off);
				name_len = i - name_off;
				break;
			}

			after_off = i;
			assert(i > 1);
			assert(i > name_off);
			name_len = i - name_off;
			break;
		}

		name = strndup(string + name_off, name_len);
		if (name == NULL)
			log_err(1, "strndup");
		value = defined_find(name);
		if (value == NULL) {
			log_warnx("undefined variable ${%s}", name);
			return (NULL);
		}

		/*
		 * Concatenate it back.
		 */
		ret = asprintf(&expanded, "%.*s%s%s",
		    before_len, string, value, string + after_off);
		if (ret < 0)
			log_err(1, "asprintf");

		//log_debugx("\"%s\" expanded to \"%s\"", string, expanded);
		free(name);

		/*
		 * Figure out where to start searching for next variable.
		 */
		string = expanded;
		i = before_len + strlen(value);
		backslashed = bracketed = false;
		before_len = name_off = name_len = after_off = 0;
		assert(i <= (int)strlen(string));
	}

	if (before_len != 0 || name_off != 0 || name_len != 0 || after_off != 0) {
		log_warnx("truncated variable");
		return (NULL);
	}

	return (expanded);
}

static void
defined_add(const char *name, const char *value)
{
	struct defined_value *d;
	const char *found;

	found = defined_find(name);
	if (found != NULL)
		log_errx(1, "variable %s already defined", name);

	log_debugx("defining variable %s=%s", name, value);

	d = calloc(1, sizeof(*d));
	if (d == NULL)
		log_err(1, "calloc");
	d->d_name = checked_strdup(name);
	d->d_value = checked_strdup(value);

	TAILQ_INSERT_TAIL(&defined_values, d, d_next);
}

void
defined_parse_and_add(char *def)
{
	char *name, *value;

	value = def;
	name = strsep(&value, "=");

	if (value == NULL || value[0] == '\0')
		log_errx(1, "missing variable value");
	if (name == NULL || name[0] == '\0')
		log_errx(1, "missing variable name");

	defined_add(name, value);
}

void
defined_init(void)
{
	struct utsname name;
	int error;

	TAILQ_INIT(&defined_values);

	error = uname(&name);
	if (error != 0)
		log_err(1, "uname");

	defined_add("ARCH", name.machine);
	defined_add("CPU", name.machine);
	defined_add("DOLLAR", "$");
	defined_add("HOST", name.nodename);
	defined_add("OSNAME", name.sysname);
	defined_add("OSREL", name.release);
	defined_add("OSVERS", name.version);
}
