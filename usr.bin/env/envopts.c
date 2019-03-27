/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005  - Garance Alistair Drosehn <gad@FreeBSD.org>.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
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
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * official policies, either expressed or implied, of the FreeBSD Project.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/stat.h>
#include <sys/param.h>
#include <err.h>
#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "envopts.h"

static const char *
		 expand_vars(int in_thisarg, char **thisarg_p, char **dest_p,
		     const char **src_p);
static int	 is_there(char *candidate);

/*
 * The is*() routines take a parameter of 'int', but expect values in the range
 * of unsigned char.  Define some wrappers which take a value of type 'char',
 * whether signed or unsigned, and ensure the value ends up in the right range.
 */
#define	isalnumch(Anychar) isalnum((u_char)(Anychar))
#define	isalphach(Anychar) isalpha((u_char)(Anychar))
#define	isspacech(Anychar) isspace((u_char)(Anychar))

/*
 * Routine to determine if a given fully-qualified filename is executable.
 * This is copied almost verbatim from FreeBSD's usr.bin/which/which.c.
 */
static int
is_there(char *candidate)
{
        struct stat fin;

        /* XXX work around access(2) false positives for superuser */
        if (access(candidate, X_OK) == 0 &&
            stat(candidate, &fin) == 0 &&
            S_ISREG(fin.st_mode) &&
            (getuid() != 0 ||
            (fin.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0)) {
                if (env_verbosity > 1)
			fprintf(stderr, "#env   matched:\t'%s'\n", candidate);
                return (1);
        }
        return (0);
}

/**
 * Routine to search through an alternate path-list, looking for a given
 * filename to execute.  If the file is found, replace the original
 * unqualified name with a fully-qualified path.  This allows `env' to
 * execute programs from a specific strict list of possible paths, without
 * changing the value of PATH seen by the program which will be executed.
 * E.G.:
 *	#!/usr/bin/env -S-P/usr/local/bin:/usr/bin perl
 * will execute /usr/local/bin/perl or /usr/bin/perl (whichever is found
 * first), no matter what the current value of PATH is, and without
 * changing the value of PATH that the script will see when it runs.
 *
 * This is similar to the print_matches() routine in usr.bin/which/which.c.
 */
void
search_paths(char *path, char **argv)
{
        char candidate[PATH_MAX];
        const char *d;
	char *filename, *fqname;

	/* If the file has a `/' in it, then no search is done */
	filename = *argv;
	if (strchr(filename, '/') != NULL)
		return;

	if (env_verbosity > 1) {
		fprintf(stderr, "#env Searching:\t'%s'\n", path);
		fprintf(stderr, "#env  for file:\t'%s'\n", filename);
	}

	fqname = NULL;
        while ((d = strsep(&path, ":")) != NULL) {
                if (*d == '\0')
                        d = ".";
                if (snprintf(candidate, sizeof(candidate), "%s/%s", d,
                    filename) >= (int)sizeof(candidate))
                        continue;
                if (is_there(candidate)) {
                        fqname = candidate;
			break;
                }
        }

	if (fqname == NULL) {
		errno = ENOENT;
		err(127, "%s", filename);
	}
	*argv = strdup(candidate);
}

/**
 * Routine to split a string into multiple parameters, while recognizing a
 * few special characters.  It recognizes both single and double-quoted
 * strings.  This processing is designed entirely for the benefit of the
 * parsing of "#!"-lines (aka "shebang" lines == the first line of an
 * executable script).  Different operating systems parse that line in very
 * different ways, and this split-on-spaces processing is meant to provide
 * ways to specify arbitrary arguments on that line, no matter how the OS
 * parses it.
 *
 * Within a single-quoted string, the two characters "\'" are treated as
 * a literal "'" character to add to the string, and "\\" are treated as
 * a literal "\" character to add.  Other than that, all characters are
 * copied until the processing gets to a terminating "'".
 *
 * Within a double-quoted string, many more "\"-style escape sequences
 * are recognized, mostly copied from what is recognized in the `printf'
 * command.  Some OS's will not allow a literal blank character to be
 * included in the one argument that they recognize on a shebang-line,
 * so a few additional escape-sequences are defined to provide ways to
 * specify blanks.
 *
 * Within a double-quoted string "\_" is turned into a literal blank.
 * (Inside of a single-quoted string, the two characters are just copied)
 * Outside of a quoted string, "\_" is treated as both a blank, and the
 * end of the current argument.  So with a shelbang-line of:
 *		#!/usr/bin/env -SA=avalue\_perl
 * the -S value would be broken up into arguments "A=avalue" and "perl".
 */
void
split_spaces(const char *str, int *origind, int *origc, char ***origv)
{
	static const char *nullarg = "";
	const char *bq_src, *copystr, *src;
	char *dest, **newargv, *newstr, **nextarg, **oldarg;
	int addcount, bq_destlen, copychar, found_sep, in_arg, in_dq, in_sq;

	/*
	 * Ignore leading space on the string, and then malloc enough room
	 * to build a copy of it.  The copy might end up shorter than the
	 * original, due to quoted strings and '\'-processing.
	 */
	while (isspacech(*str))
		str++;
	if (*str == '\0')
		return;
	newstr = malloc(strlen(str) + 1);

	/*
	 * Allocate plenty of space for the new array of arg-pointers,
	 * and start that array off with the first element of the old
	 * array.
	 */
	newargv = malloc((*origc + (strlen(str) / 2) + 2) * sizeof(char *));
	nextarg = newargv;
	*nextarg++ = **origv;

	/* Come up with the new args by splitting up the given string. */
	addcount = 0;
	bq_destlen = in_arg = in_dq = in_sq = 0;
	bq_src = NULL;
	for (src = str, dest = newstr; *src != '\0'; src++) {
		/*
		 * This switch will look at a character in *src, and decide
		 * what should be copied to *dest.  It only decides what
		 * character(s) to copy, it should not modify *dest.  In some
		 * cases, it will look at multiple characters from *src.
		 */
		copychar = found_sep = 0;
		copystr = NULL;
		switch (*src) {
		case '"':
			if (in_sq)
				copychar = *src;
			else if (in_dq)
				in_dq = 0;
			else {
				/*
				 * Referencing nullarg ensures that a new
				 * argument is created, even if this quoted
				 * string ends up with zero characters.
				 */
				copystr = nullarg;
				in_dq = 1;
				bq_destlen = dest - *(nextarg - 1);
				bq_src = src;
			}
			break;
		case '$':
			if (in_sq)
				copychar = *src;
			else {
				copystr = expand_vars(in_arg, (nextarg - 1),
				    &dest, &src);
			}
			break;
		case '\'':
			if (in_dq)
				copychar = *src;
			else if (in_sq)
				in_sq = 0;
			else {
				/*
				 * Referencing nullarg ensures that a new
				 * argument is created, even if this quoted
				 * string ends up with zero characters.
				 */
				copystr = nullarg;
				in_sq = 1;
				bq_destlen = dest - *(nextarg - 1);
				bq_src = src;
			}
			break;
		case '\\':
			if (in_sq) {
				/*
				 * Inside single-quoted strings, only the
				 * "\'" and "\\" are recognized as special
				 * strings.
				 */
				copychar = *(src + 1);
				if (copychar == '\'' || copychar == '\\')
					src++;
				else
					copychar = *src;
				break;
			}
			src++;
			switch (*src) {
			case '"':
			case '#':
			case '$':
			case '\'':
			case '\\':
				copychar = *src;
				break;
			case '_':
				/*
				 * Alternate way to get a blank, which allows
				 * that blank be used to separate arguments
				 * when it is not inside a quoted string.
				 */
				if (in_dq)
					copychar = ' ';
				else {
					found_sep = 1;
					src++;
				}
				break;
			case 'c':
				/*
				 * Ignore remaining characters in the -S string.
				 * This would not make sense if found in the
				 * middle of a quoted string.
				 */
				if (in_dq)
					errx(1, "Sequence '\\%c' is not allowed"
					    " in quoted strings", *src);
				goto str_done;
			case 'f':
				copychar = '\f';
				break;
			case 'n':
				copychar = '\n';
				break;
			case 'r':
				copychar = '\r';
				break;
			case 't':
				copychar = '\t';
				break;
			case 'v':
				copychar = '\v';
				break;
			default:
				if (isspacech(*src))
					copychar = *src;
				else
					errx(1, "Invalid sequence '\\%c' in -S",
					    *src);
			}
			break;
		default:
			if ((in_dq || in_sq) && in_arg)
				copychar = *src;
			else if (isspacech(*src))
				found_sep = 1;
			else {
				/*
				 * If the first character of a new argument
				 * is `#', then ignore the remaining chars.
				 */
				if (!in_arg && *src == '#')
					goto str_done;
				copychar = *src;
			}
		}
		/*
		 * Now that the switch has determined what (if anything)
		 * needs to be copied, copy whatever that is to *dest.
		 */
		if (copychar || copystr != NULL) {
			if (!in_arg) {
				/* This is the first byte of a new argument */
				*nextarg++ = dest;
				addcount++;
				in_arg = 1;
			}
			if (copychar)
				*dest++ = (char)copychar;
			else if (copystr != NULL)
				while (*copystr != '\0')
					*dest++ = *copystr++;
		} else if (found_sep) {
			*dest++ = '\0';
			while (isspacech(*src))
				src++;
			--src;
			in_arg = 0;
		}
	}
str_done:
	*dest = '\0';
	*nextarg = NULL;
	if (in_dq || in_sq) {
		errx(1, "No terminating quote for string: %.*s%s",
		    bq_destlen, *(nextarg - 1), bq_src);
	}
	if (env_verbosity > 1) {
		fprintf(stderr, "#env  split -S:\t'%s'\n", str);
		oldarg = newargv + 1;
		fprintf(stderr, "#env      into:\t'%s'\n", *oldarg);
		for (oldarg++; *oldarg; oldarg++)
			fprintf(stderr, "#env          &\t'%s'\n", *oldarg);
	}

	/* Copy the unprocessed arg-pointers from the original array */
	for (oldarg = *origv + *origind; *oldarg; oldarg++)
		*nextarg++ = *oldarg;
	*nextarg = NULL;

	/* Update optind/argc/argv in the calling routine */
	*origc += addcount - *origind + 1;
	*origv = newargv;
	*origind = 1;
}

/**
 * Routine to split expand any environment variables referenced in the string
 * that -S is processing.  For now it only supports the form ${VARNAME}.  It
 * explicitly does not support $VARNAME, and obviously can not handle special
 * shell-variables such as $?, $*, $1, etc.  It is called with *src_p pointing
 * at the initial '$', and if successful it will update *src_p, *dest_p, and
 * possibly *thisarg_p in the calling routine.
 */
static const char *
expand_vars(int in_thisarg, char **thisarg_p, char **dest_p, const char **src_p)
{
	const char *vbegin, *vend, *vvalue;
	char *newstr, *vname;
	int bad_reference;
	size_t namelen, newlen;

	bad_reference = 1;
	vbegin = vend = (*src_p) + 1;
	if (*vbegin++ == '{')
		if (*vbegin == '_' || isalphach(*vbegin)) {
			vend = vbegin + 1;
			while (*vend == '_' || isalnumch(*vend))
				vend++;
			if (*vend == '}')
				bad_reference = 0;
		}
	if (bad_reference)
		errx(1, "Only ${VARNAME} expansion is supported, error at: %s",
		    *src_p);

	/*
	 * We now know we have a valid environment variable name, so update
	 * the caller's source-pointer to the last character in that reference,
	 * and then pick up the matching value.  If the variable is not found,
	 * or if it has a null value, then our work here is done.
	 */
	*src_p = vend;
	namelen = vend - vbegin + 1;
	vname = malloc(namelen);
	strlcpy(vname, vbegin, namelen);
	vvalue = getenv(vname);
	if (vvalue == NULL || *vvalue == '\0') {
		if (env_verbosity > 2)
			fprintf(stderr,
			    "#env  replacing ${%s} with null string\n",
			    vname);
		free(vname);
		return (NULL);
	}

	if (env_verbosity > 2)
		fprintf(stderr, "#env  expanding ${%s} into '%s'\n", vname,
		    vvalue);

	/*
	 * There is some value to copy to the destination.  If the value is
	 * shorter than the ${VARNAME} reference that it replaces, then our
	 * caller can just copy the value to the existing destination.
	 */
	if (strlen(vname) + 3 >= strlen(vvalue)) {
		free(vname);
		return (vvalue);
	}

	/*
	 * The value is longer than the string it replaces, which means the
	 * present destination area is too small to hold it.  Create a new
	 * destination area, and update the caller's 'dest' variable to match.
	 * If the caller has already started copying some info for 'thisarg'
	 * into the present destination, then the new destination area must
	 * include a copy of that data, and the pointer to 'thisarg' must also
	 * be updated.  Note that it is still the caller which copies this
	 * vvalue to the new *dest.
	 */
	newlen = strlen(vvalue) + strlen(*src_p) + 1;
	if (in_thisarg) {
		**dest_p = '\0';	/* Provide terminator for 'thisarg' */
		newlen += strlen(*thisarg_p);
		newstr = malloc(newlen);
		strcpy(newstr, *thisarg_p);
		*thisarg_p = newstr;
	} else {
		newstr = malloc(newlen);
		*newstr = '\0';
	}
	*dest_p = strchr(newstr, '\0');
	free(vname);
	return (vvalue);
}
