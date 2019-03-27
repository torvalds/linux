/*-
 * Copyright (c) 2001-2014 Devin Teske <dteske@FreeBSD.org>
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

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "string_m.h"

/*
 * Counts the number of occurrences of one string that appear in the source
 * string. Return value is the total count.
 *
 * An example use would be if you need to know how large a block of memory
 * needs to be for a replaceall() series.
 */
unsigned int
strcount(const char *source, const char *find)
{
	const char *p = source;
	size_t flen;
	unsigned int n = 0;

	/* Both parameters are required */
	if (source == NULL || find == NULL)
		return (0);

	/* Cache the length of find element */
	flen = strlen(find);
	if (strlen(source) == 0 || flen == 0)
		return (0);

	/* Loop until the end of the string */
	while (*p != '\0') {
		if (strncmp(p, find, flen) == 0) { /* found an instance */
			p += flen;
			n++;
		} else
			p++;
	}

	return (n);
}

/*
 * Replaces all occurrences of `find' in `source' with `replace'.
 *
 * You should not pass a string constant as the first parameter, it needs to be
 * a pointer to an allocated block of memory. The block of memory that source
 * points to should be large enough to hold the result. If the length of the
 * replacement string is greater than the length of the find string, the result
 * will be larger than the original source string. To allocate enough space for
 * the result, use the function strcount() declared above to determine the
 * number of occurrences and how much larger the block size needs to be.
 *
 * If source is not large enough, the application will crash. The return value
 * is the length (in bytes) of the result.
 *
 * When an error occurs, -1 is returned and the global variable errno is set
 * accordingly. Returns zero on success.
 */
int
replaceall(char *source, const char *find, const char *replace)
{
	char *p;
	char *t;
	char *temp;
	size_t flen;
	size_t rlen;
	size_t slen;
	uint32_t n = 0;

	errno = 0; /* reset global error number */

	/* Check that we have non-null parameters */
	if (source == NULL)
		return (0);
	if (find == NULL)
		return (strlen(source));

	/* Cache the length of the strings */
	slen = strlen(source);
	flen = strlen(find);
	rlen = replace ? strlen(replace) : 0;

	/* Cases where no replacements need to be made */
	if (slen == 0 || flen == 0 || slen < flen)
		return (slen);

	/* If replace is longer than find, we'll need to create a temp copy */
	if (rlen > flen) {
		temp = malloc(slen + 1);
		if (temp == NULL) /* could not allocate memory */
			return (-1);
		memcpy(temp, source, slen + 1);
	} else
		temp = source;

	/* Reconstruct the string with the replacements */
	p = source; t = temp; /* position elements */
 
	while (*t != '\0') {
		if (strncmp(t, find, flen) == 0) {
			/* found an occurrence */
			for (n = 0; replace && replace[n]; n++)
				*p++ = replace[n];
			t += flen;
		} else
			*p++ = *t++; /* copy character and increment */
	}

	/* Terminate the string */
	*p = '\0';

	/* Free the temporary allocated memory */
	if (temp != source)
		free(temp);

	/* Return the length of the completed string */
	return (strlen(source));
}

/*
 * Expands escape sequences in a buffer pointed to by `source'. This function
 * steps through each character, and converts escape sequences such as "\n",
 * "\r", "\t" and others into their respective meanings.
 *
 * You should not pass a string constant or literal to this function or the
 * program will likely segmentation fault when it tries to modify the data.
 *
 * The string length will either shorten or stay the same depending on whether
 * any escape sequences were converted but the amount of memory allocated does
 * not change.
 *
 * Interpreted sequences are:
 *
 * 	\0NNN	character with octal value NNN (0 to 3 digits)
 * 	\N	character with octal value N (0 thru 7)
 * 	\a	alert (BEL)
 * 	\b	backslash
 * 	\f	form feed
 * 	\n	new line
 * 	\r	carriage return
 * 	\t	horizontal tab
 * 	\v	vertical tab
 * 	\xNN	byte with hexadecimal value NN (1 to 2 digits)
 *
 * All other sequences are unescaped (ie. '\"' and '\#').
 */
void strexpand(char *source)
{
	uint8_t c;
	char *chr;
	char *pos;
	char d[4];

	/* Initialize position elements */
	pos = chr = source;

	/* Loop until we hit the end of the string */
	while (*pos != '\0') {
		if (*chr != '\\') {
			*pos = *chr; /* copy character to current offset */
			pos++;
			chr++;
			continue;
		}

		/* Replace the backslash with the correct character */
		switch (*++chr) {
		case 'a': *pos = '\a'; break; /* bell/alert (BEL) */
		case 'b': *pos = '\b'; break; /* backspace */
		case 'f': *pos = '\f'; break; /* form feed */
		case 'n': *pos = '\n'; break; /* new line */
		case 'r': *pos = '\r'; break; /* carriage return */
		case 't': *pos = '\t'; break; /* horizontal tab */
		case 'v': *pos = '\v'; break; /* vertical tab */
		case 'x': /* hex value (1 to 2 digits)(\xNN) */
			d[2] = '\0'; /* pre-terminate the string */

			/* verify next two characters are hex */
			d[0] = isxdigit(*(chr+1)) ? *++chr : '\0';
			if (d[0] != '\0')
				d[1] = isxdigit(*(chr+1)) ? *++chr : '\0';

			/* convert the characters to decimal */
			c = (uint8_t)strtoul(d, 0, 16);

			/* assign the converted value */
			*pos = (c != 0 || d[0] == '0') ? c : *++chr;
			break;
		case '0': /* octal value (0 to 3 digits)(\0NNN) */
			d[3] = '\0'; /* pre-terminate the string */

			/* verify next three characters are octal */
			d[0] = (isdigit(*(chr+1)) && *(chr+1) < '8') ?
			    *++chr : '\0';
			if (d[0] != '\0')
				d[1] = (isdigit(*(chr+1)) && *(chr+1) < '8') ?
				    *++chr : '\0';
			if (d[1] != '\0')
				d[2] = (isdigit(*(chr+1)) && *(chr+1) < '8') ?
				    *++chr : '\0';

			/* convert the characters to decimal */
			c = (uint8_t)strtoul(d, 0, 8);

			/* assign the converted value */
			*pos = c;
			break;
		default: /* single octal (\0..7) or unknown sequence */
			if (isdigit(*chr) && *chr < '8') {
				d[0] = *chr;
				d[1] = '\0';
				*pos = (uint8_t)strtoul(d, 0, 8);
			} else
				*pos = *chr;
		}

		/* Increment to next offset, possible next escape sequence */
		pos++;
		chr++;
	}
}

/*
 * Expand only the escaped newlines in a buffer pointed to by `source'. This
 * function steps through each character, and converts the "\n" sequence into
 * a literal newline and the "\\n" sequence into "\n".
 *
 * You should not pass a string constant or literal to this function or the
 * program will likely segmentation fault when it tries to modify the data.
 *
 * The string length will either shorten or stay the same depending on whether
 * any escaped newlines were converted but the amount of memory allocated does
 * not change.
 */
void strexpandnl(char *source)
{
	uint8_t backslash = 0;
	char *cp1;
	char *cp2;
	
	/* Replace '\n' with literal in dprompt */
	cp1 = cp2 = source;
	while (*cp2 != '\0') {
		*cp1 = *cp2;
		if (*cp2 == '\\')
			backslash++;
		else if (*cp2 != 'n')
			backslash = 0;
		else if (backslash > 0) {
			*(--cp1) = (backslash & 1) == 1 ? '\n' : 'n';
			backslash = 0;
		}
		cp1++;
		cp2++;
	}
	*cp1 = *cp2;
}

/*
 * Convert a string to lower case. You should not pass a string constant to
 * this function. Only pass pointers to allocated memory with null terminated
 * string data.
 */
void
strtolower(char *source)
{
	char *p = source;

	if (source == NULL)
		return;

	while (*p != '\0') {
		*p = tolower(*p);
		p++; /* would have just used `*p++' but gcc 3.x warns */
	}
}
