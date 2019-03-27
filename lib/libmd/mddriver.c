/* MDDRIVER.C - test driver for MD2, MD4 and MD5 */

/* Copyright (C) 1990-2, RSA Data Security, Inc. Created 1990. All rights
 * reserved.
 * 
 * RSA Data Security, Inc. makes no representations concerning either the
 * merchantability of this software or the suitability of this software for
 * any particular purpose. It is provided "as is" without express or implied
 * warranty of any kind.
 * 
 * These notices must be retained in any copies of any part of this
 * documentation and/or software. */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>

#include <stdio.h>
#include <time.h>
#include <string.h>

/* The following makes MD default to MD5 if it has not already been defined
 * with C compiler flags. */
#ifndef MD
#define MD 5
#endif

#if MD == 2
#include "md2.h"
#define MDData MD2Data
#endif
#if MD == 4
#include "md4.h"
#define MDData MD4Data
#endif
#if MD == 5
#include "md5.h"
#define MDData MD5Data
#endif

/* Digests a string and prints the result. */
static void 
MDString(char *string)
{
	char buf[33];

	printf("MD%d (\"%s\") = %s\n",
	       MD, string, MDData(string, strlen(string), buf));
}

/* Digests a reference suite of strings and prints the results. */
int
main(void)
{
	printf("MD%d test suite:\n", MD);

	MDString("");
	MDString("a");
	MDString("abc");
	MDString("message digest");
	MDString("abcdefghijklmnopqrstuvwxyz");
	MDString("ABCDEFGHIJKLMNOPQRSTUVWXYZ"
		"abcdefghijklmnopqrstuvwxyz0123456789");
	MDString("1234567890123456789012345678901234567890"
		"1234567890123456789012345678901234567890");

	return 0;
}
