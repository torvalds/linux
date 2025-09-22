/*	$OpenBSD: prftest.c,v 1.3 2021/12/13 16:56:49 deraadt Exp $	*/
/*	$EOM: prftest.c,v 1.2 1998/10/07 16:40:50 niklas Exp $	*/

/*
 * Copyright (c) 1998 Niels Provos.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This code was written under funding by Ericsson Radio Systems.
 */

#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "hash.h"
#include "prf.h"

int test_prf (char *, enum hashes, char *, int, char *, int, char *);

#define nibble2c(x) ((x) >= 10 ? ('a'-10+(x)) : ('0' + (x)))

/*
 * Basically the same as the HMAC regress, but to keep with modularity
 * prf seems to be useful. So here we just check the HMAC test cases,
 * until there are more PRFs.
 */

int
main (void)
{
  char key[100];

  memset (key, 11, 20);
  test_prf ("PRF MD5 Test Case 1", HASH_MD5,
	    key, 16, "Hi There", 8, "9294727a3638bb1c13f48ef8158bfc9d");
  test_prf ("PRF MD5 Test Case 2", HASH_MD5,
	    "Jefe", 4,
	    "what do ya want for nothing?", 28,
	    "750c783e6ab0b503eaa86e310a5db738");
  test_prf ("PRF SHA1 Test Case 1", HASH_SHA1,
	    key, 20, "Hi There", 8,
	    "b617318655057264e28bc0b6fb378c8ef146be00");
  test_prf ("PRF SHA1 Test Case 2",  HASH_SHA1,
	    "Jefe", 4, "what do ya want for nothing?", 28,
	    "effcdf6ae5eb2fa2d27416d5f184df9c259a7c79");
  test_prf ("PRF SHA1 Test Case 3",  HASH_SHA1,
	    "Bloody long key, this one, eben longer than the blocksize "
	    "of ordinary keyed HMAC functions", 90,
	    "what do ya want for nothing?", 28,
	    "52ca5fbcd7d4821bc6bf8b6e95e131109dff901b");

  return 0;
}

int
test_prf (char *test, enum hashes hash, char *key, int klen,
	  char *data, int dlen, char *cmp)
{
  char output[2*HASH_MAX+1];
  char digest[HASH_MAX];
  struct prf *prf;
  int i;

  printf ("Testing %s: ", test);

  prf = prf_alloc (PRF_HMAC, hash, key, klen);
  if (!prf)
    {
      printf("prf_alloc () failed\n");
      return 0;
    }

  prf->Init (prf->prfctx);
  prf->Update (prf->prfctx, data, dlen);
  prf->Final (digest, prf->prfctx);

  for (i = 0; i < prf->blocksize; i++)
    {
      output[2 * i] = nibble2c ((digest[i] >> 4) & 0xf);
      output[2 * i + 1] = nibble2c (digest[i] & 0xf);
    }
  output[2 * i] = 0;

  prf_free (prf);

  if (strcmp (output, cmp) == 0)
    {
      printf ("OKAY\n");
      return 1;
    }

  printf ("%s <-> %s\n", output, cmp);
  return 0;
}
