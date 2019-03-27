/*-
 * Copyright (c) 2002-2015 Devin Teske <dteske@FreeBSD.org>
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

#include <sys/param.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "figpar.h"
#include "string_m.h"

struct figpar_config figpar_dummy_config = {0, NULL, {0}, NULL};

/*
 * Search for config option (struct figpar_config) in the array of config
 * options, returning the struct whose directive matches the given parameter.
 * If no match is found, a pointer to the static dummy array (above) is
 * returned.
 *
 * This is to eliminate dependency on the index position of an item in the
 * array, since the index position is more apt to be changed as code grows.
 */
struct figpar_config *
get_config_option(struct figpar_config options[], const char *directive)
{
	uint32_t n;

	/* Check arguments */
	if (options == NULL || directive == NULL)
		return (&figpar_dummy_config);

	/* Loop through the array, return the index of the first match */
	for (n = 0; options[n].directive != NULL; n++)
		if (strcmp(options[n].directive, directive) == 0)
			return (&(options[n]));

	/* Re-initialize the dummy variable in case it was written to */
	figpar_dummy_config.directive	= NULL;
	figpar_dummy_config.type	= 0;
	figpar_dummy_config.action	= NULL;
	figpar_dummy_config.value.u_num	= 0;

	return (&figpar_dummy_config);
}

/*
 * Parse the configuration file at `path' and execute the `action' call-back
 * functions for any directives defined by the array of config options (first
 * argument).
 *
 * For unknown directives that are encountered, you can optionally pass a
 * call-back function for the third argument to be called for unknowns.
 *
 * Returns zero on success; otherwise returns -1 and errno should be consulted.
*/
int
parse_config(struct figpar_config options[], const char *path,
    int (*unknown)(struct figpar_config *option, uint32_t line,
    char *directive, char *value), uint16_t processing_options)
{
	uint8_t bequals;
	uint8_t bsemicolon;
	uint8_t case_sensitive;
	uint8_t comment = 0;
	uint8_t end;
	uint8_t found;
	uint8_t have_equals = 0;
	uint8_t quote;
	uint8_t require_equals;
	uint8_t strict_equals;
	char p[2];
	char *directive;
	char *t;
	char *value;
	int error;
	int fd;
	ssize_t r = 1;
	uint32_t dsize;
	uint32_t line = 1;
	uint32_t n;
	uint32_t vsize;
	uint32_t x;
	off_t charpos;
	off_t curpos;
	char rpath[PATH_MAX];

	/* Sanity check: if no options and no unknown function, return */
	if (options == NULL && unknown == NULL)
		return (-1);

	/* Processing options */
	bequals = (processing_options & FIGPAR_BREAK_ON_EQUALS) == 0 ? 0 : 1;
	bsemicolon =
		(processing_options & FIGPAR_BREAK_ON_SEMICOLON) == 0 ? 0 : 1;
	case_sensitive =
		(processing_options & FIGPAR_CASE_SENSITIVE) == 0 ? 0 : 1;
	require_equals =
		(processing_options & FIGPAR_REQUIRE_EQUALS) == 0 ? 0 : 1;
	strict_equals =
		(processing_options & FIGPAR_STRICT_EQUALS) == 0 ? 0 : 1;

	/* Initialize strings */
	directive = value = 0;
	vsize = dsize = 0;

	/* Resolve the file path */
	if (realpath(path, rpath) == 0)
		return (-1);

	/* Open the file */
	if ((fd = open(rpath, O_RDONLY)) < 0)
		return (-1);

	/* Read the file until EOF */
	while (r != 0) {
		r = read(fd, p, 1);

		/* skip to the beginning of a directive */
		while (r != 0 && (isspace(*p) || *p == '#' || comment ||
		    (bsemicolon && *p == ';'))) {
			if (*p == '#')
				comment = 1;
			else if (*p == '\n') {
				comment = 0;
				line++;
			}
			r = read(fd, p, 1);
		}
		/* Test for EOF; if EOF then no directive was found */
		if (r == 0) {
			close(fd);
			return (0);
		}

		/* Get the current offset */
		curpos = lseek(fd, 0, SEEK_CUR) - 1;
		if (curpos == -1) {
			close(fd);
			return (-1);
		}

		/* Find the length of the directive */
		for (n = 0; r != 0; n++) {
			if (isspace(*p))
				break;
			if (bequals && *p == '=') {
				have_equals = 1;
				break;
			}
			if (bsemicolon && *p == ';')
				break;
			r = read(fd, p, 1);
		}

		/* Test for EOF, if EOF then no directive was found */
		if (n == 0 && r == 0) {
			close(fd);
			return (0);
		}

		/* Go back to the beginning of the directive */
		error = (int)lseek(fd, curpos, SEEK_SET);
		if (error == (curpos - 1)) {
			close(fd);
			return (-1);
		}

		/* Allocate and read the directive into memory */
		if (n > dsize) {
			if ((directive = realloc(directive, n + 1)) == NULL) {
				close(fd);
				return (-1);
			}
			dsize = n;
		}
		r = read(fd, directive, n);

		/* Advance beyond the equals sign if appropriate/desired */
		if (bequals && *p == '=') {
			if (lseek(fd, 1, SEEK_CUR) != -1)
				r = read(fd, p, 1);
			if (strict_equals && isspace(*p))
				*p = '\n';
		}

		/* Terminate the string */
		directive[n] = '\0';

		/* Convert directive to lower case before comparison */
		if (!case_sensitive)
			strtolower(directive);

		/* Move to what may be the start of the value */
		if (!(bsemicolon && *p == ';') &&
		    !(strict_equals && *p == '=')) {
			while (r != 0 && isspace(*p) && *p != '\n')
				r = read(fd, p, 1);
		}

		/* An equals sign may have stopped us, should we eat it? */
		if (r != 0 && bequals && *p == '=' && !strict_equals) {
			have_equals = 1;
			r = read(fd, p, 1);
			while (r != 0 && isspace(*p) && *p != '\n')
				r = read(fd, p, 1);
		}

		/* If no value, allocate a dummy value and jump to action */
		if (r == 0 || *p == '\n' || *p == '#' ||
		    (bsemicolon && *p == ';')) {
			/* Initialize the value if not already done */
			if (value == NULL && (value = malloc(1)) == NULL) {
				close(fd);
				return (-1);
			}
			value[0] = '\0';
			goto call_function;
		}

		/* Get the current offset */
		curpos = lseek(fd, 0, SEEK_CUR) - 1;
		if (curpos == -1) {
			close(fd);
			return (-1);
		}

		/* Find the end of the value */
		quote = 0;
		end = 0;
		while (r != 0 && end == 0) {
			/* Advance to the next character if we know we can */
			if (*p != '\"' && *p != '#' && *p != '\n' &&
			    (!bsemicolon || *p != ';')) {
				r = read(fd, p, 1);
				continue;
			}

			/*
			 * If we get this far, we've hit an end-key
			 */

			/* Get the current offset */
			charpos = lseek(fd, 0, SEEK_CUR) - 1;
			if (charpos == -1) {
				close(fd);
				return (-1);
			}

			/*
			 * Go back so we can read the character before the key
			 * to check if the character is escaped (which means we
			 * should continue).
			 */
			error = (int)lseek(fd, -2, SEEK_CUR);
			if (error == -3) {
				close(fd);
				return (-1);
			}
			r = read(fd, p, 1);

			/*
			 * Count how many backslashes there are (an odd number
			 * means the key is escaped, even means otherwise).
			 */
			for (n = 1; *p == '\\'; n++) {
				/* Move back another offset to read */
				error = (int)lseek(fd, -2, SEEK_CUR);
				if (error == -3) {
					close(fd);
					return (-1);
				}
				r = read(fd, p, 1);
			}

			/* Move offset back to the key and read it */
			error = (int)lseek(fd, charpos, SEEK_SET);
			if (error == (charpos - 1)) {
				close(fd);
				return (-1);
			}
			r = read(fd, p, 1);

			/*
			 * If an even number of backslashes was counted meaning
			 * key is not escaped, we should evaluate what to do.
			 */
			if ((n & 1) == 1) {
				switch (*p) {
				case '\"':
					/*
				 	 * Flag current sequence of characters
					 * to follow as being quoted (hashes
					 * are not considered comments).
					 */
					quote = !quote;
					break;
				case '#':
					/*
					 * If we aren't in a quoted series, we
					 * just hit an inline comment and have
					 * found the end of the value.
					 */
					if (!quote)
						end = 1;
					break;
				case '\n':
					/*
					 * Newline characters must always be
					 * escaped, whether inside a quoted
					 * series or not, otherwise they
					 * terminate the value.
					 */
					end = 1;
				case ';':
					if (!quote && bsemicolon)
						end = 1;
					break;
				}
			} else if (*p == '\n')
				/* Escaped newline character. increment */
				line++;

			/* Advance to the next character */
			r = read(fd, p, 1);
		}

		/* Get the current offset */
		charpos = lseek(fd, 0, SEEK_CUR) - 1;
		if (charpos == -1) {
			close(fd);
			return (-1);
		}

		/* Get the length of the value */
		n = (uint32_t)(charpos - curpos);
		if (r != 0) /* more to read, but don't read ending key */
			n--;

		/* Move offset back to the beginning of the value */
		error = (int)lseek(fd, curpos, SEEK_SET);
		if (error == (curpos - 1)) {
			close(fd);
			return (-1);
		}

		/* Allocate and read the value into memory */
		if (n > vsize) {
			if ((value = realloc(value, n + 1)) == NULL) {
				close(fd);
				return (-1);
			}
			vsize = n;
		}
		r = read(fd, value, n);

		/* Terminate the string */
		value[n] = '\0';

		/* Cut trailing whitespace off by termination */
		t = value + n;
		while (isspace(*--t))
			*t = '\0';

		/* Escape the escaped quotes (replaceall is in string_m.c) */
		x = strcount(value, "\\\""); /* in string_m.c */
		if (x != 0 && (n + x) > vsize) {
			if ((value = realloc(value, n + x + 1)) == NULL) {
				close(fd);
				return (-1);
			}
			vsize = n + x;
		}
		if (replaceall(value, "\\\"", "\\\\\"") < 0) {
			/* Replace operation failed for some unknown reason */
			close(fd);
			return (-1);
		}

		/* Remove all new line characters */
		if (replaceall(value, "\\\n", "") < 0) {
			/* Replace operation failed for some unknown reason */
			close(fd);
			return (-1);
		}

		/* Resolve escape sequences */
		strexpand(value); /* in string_m.c */

call_function:
		/* Abort if we're seeking only assignments */
		if (require_equals && !have_equals)
			return (-1);

		found = have_equals = 0; /* reset */

		/* If there are no options defined, call unknown and loop */
		if (options == NULL && unknown != NULL) {
			error = unknown(NULL, line, directive, value);
			if (error != 0) {
				close(fd);
				return (error);
			}
			continue;
		}

		/* Loop through the array looking for a match for the value */
		for (n = 0; options[n].directive != NULL; n++) {
			error = fnmatch(options[n].directive, directive,
			    FNM_NOESCAPE);
			if (error == 0) {
				found = 1;
				/* Call function for array index item */
				if (options[n].action != NULL) {
					error = options[n].action(
					    &options[n],
					    line, directive, value);
					if (error != 0) {
						close(fd);
						return (error);
					}
				}
			} else if (error != FNM_NOMATCH) {
				/* An error has occurred */
				close(fd);
				return (-1);
			}
		}
		if (!found && unknown != NULL) {
			/*
			 * No match was found for the value we read from the
			 * file; call function designated for unknown values.
			 */
			error = unknown(NULL, line, directive, value);
			if (error != 0) {
				close(fd);
				return (error);
			}
		}
	}

	close(fd);
	return (0);
}
