/* SHADRIVER.C - test driver for SHA-1 (and SHA-2) */

/*-
 * SPDX-License-Identifier: RSA-MD
 *
 * Copyright (C) 1990-2, RSA Data Security, Inc. Created 1990. All rights
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

#include "sha.h"
#include "sha224.h"
#include "sha256.h"
#include "sha384.h"
#include "sha512.h"
#include "sha512t.h"

/* The following makes SHA default to SHA-1 if it has not already been
 * defined with C compiler flags. */
#ifndef SHA
#define SHA 1
#endif

#if SHA == 1
#undef SHA_Data
#define SHA_Data SHA1_Data
#elif SHA == 224
#undef SHA_Data
#define SHA_Data SHA224_Data
#elif SHA == 256
#undef SHA_Data
#define SHA_Data SHA256_Data
#elif SHA == 384
#undef SHA_Data
#define SHA_Data SHA384_Data
#elif SHA == 512
#undef SHA_Data
#define SHA_Data SHA512_Data
#elif SHA == 512256
#undef SHA_Data
#define SHA_Data SHA512_256_Data
#endif

/* Digests a string and prints the result. */
static void
SHAString(char *string)
{
	char buf[2*64 + 1];

	printf("SHA-%d (\"%s\") = %s\n",
	       SHA, string, SHA_Data(string, strlen(string), buf));
}

/* Digests a reference suite of strings and prints the results. */
int
main(void)
{
	printf("SHA-%d test suite:\n", SHA);

	SHAString("");
	SHAString("abc");
	SHAString("message digest");
	SHAString("abcdefghijklmnopqrstuvwxyz");
	SHAString("ABCDEFGHIJKLMNOPQRSTUVWXYZ"
		  "abcdefghijklmnopqrstuvwxyz0123456789");
	SHAString("1234567890123456789012345678901234567890"
		  "1234567890123456789012345678901234567890");

	return 0;
}
