/* $OpenBSD: base64.c,v 1.13 2024/10/22 22:33:06 jsg Exp $ */
/*
 * The author of this code is Angelos D. Keromytis (angelos@dsl.cis.upenn.edu)
 *
 * This code was written by Angelos D. Keromytis in Philadelphia, PA, USA,
 * in April-May 1998
 *
 * Copyright (C) 1998, 1999 by Angelos D. Keromytis.
 *	
 * Permission to use, copy, and modify this software with or without fee
 * is hereby granted, provided that this entire notice is included in
 * all copies of any software which is or includes a copy or
 * modification of this software. 
 *
 * THIS SOFTWARE IS BEING PROVIDED "AS IS", WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTY. IN PARTICULAR, THE AUTHORS MAKES NO
 * REPRESENTATION OR WARRANTY OF ANY KIND CONCERNING THE
 * MERCHANTABILITY OF THIS SOFTWARE OR ITS FITNESS FOR ANY PARTICULAR
 * PURPOSE.
 */

#include <sys/types.h>

#include <ctype.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "keynote.h"

int __b64_ntop(unsigned char const *, size_t, char *, size_t);
int __b64_pton(char const *, unsigned char *, size_t);

int
kn_encode_base64(unsigned char const *src, unsigned int srclength, char *target,
    unsigned int targsize)
{
    int i;

    i = __b64_ntop(src, srclength, target, targsize);
    if (i == -1)
      keynote_errno = ERROR_SYNTAX;
    return i;
}

int
kn_decode_base64(char const *src, unsigned char *target, unsigned int targsize)
{
    int i;

    i = __b64_pton(src, target, targsize);
    if (i == -1)
      keynote_errno = ERROR_SYNTAX;
    return i;
}
