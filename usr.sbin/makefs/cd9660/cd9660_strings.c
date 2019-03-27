/*	$NetBSD: cd9660_strings.c,v 1.4 2007/01/16 17:32:05 hubertf Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 2005 Daniel Watt, Walter Deignan, Ryan Gabrys, Alan
 * Perez-Rathke and Ram Vedam.  All rights reserved.
 *
 * This code was written by Daniel Watt, Walter Deignan, Ryan Gabrys,
 * Alan Perez-Rathke and Ram Vedam.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY DANIEL WATT, WALTER DEIGNAN, RYAN
 * GABRYS, ALAN PEREZ-RATHKE AND RAM VEDAM ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL DANIEL WATT, WALTER DEIGNAN, RYAN
 * GABRYS, ALAN PEREZ-RATHKE AND RAM VEDAM BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE,DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 */

#include <sys/mount.h>

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <sys/param.h>
#include <ctype.h>

#include "makefs.h"
#include "cd9660.h"


void
cd9660_uppercase_characters(char *str, int len)
{
	int p;

	for (p = 0; p < len; p++) {
		if (islower((unsigned char)str[p]) )
			str[p] -= 32;
	}
}

static inline int
cd9660_is_d_char(char c)
{
	return (isupper((unsigned char)c)
		|| c == '_'
		|| (c >= '0' && c <= '9'));
}

static inline int
cd9660_is_a_char(char c)
{
	return (isupper((unsigned char)c)
			|| c == '_'
			|| (c >= '%' && c <= '?')
			|| (c >= ' ' && c <= '\"'));
}

/*
 * Test a string to see if it is composed of valid a characters
 * @param const char* The string to test
 * @returns int 1 if valid, 2 if valid if characters are converted to
 *              upper case, 0 otherwise
 */
int
cd9660_valid_a_chars(const char *str)
{
	const char *c = str;
	int upperFound = 0;

	while ((*c) != '\0') {
		if (!(cd9660_is_a_char(*c))) {
			if (islower((unsigned char)*c) )
				upperFound = 1;
			else
				return 0;
		}
		c++;
	}
	return upperFound + 1;
}

/*
 * Test a string to see if it is composed of valid d characters
 * @param const char* The string to test
 * @returns int 1 if valid, 2 if valid if characters are converted to
 *                upper case, 0 otherwise
 */
int
cd9660_valid_d_chars(const char *str)
{
	const char *c=str;
	int upperFound = 0;

	while ((*c) != '\0') {
		if (!(cd9660_is_d_char(*c))) {
			if (islower((unsigned char)*c) )
				upperFound = 1;
			else
				return 0;
		}
		c++;
	}
	return upperFound + 1;
}
