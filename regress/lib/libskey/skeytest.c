/*	$OpenBSD: skeytest.c,v 1.4 2014/03/25 04:29:49 lteo Exp $	*/
/*	$NetBSD: skeytest.c,v 1.3 2002/02/21 07:38:18 itojun Exp $	*/

/*-
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This is a regression test for the S/Key implementation against the data set
 * from Appendix C of RFC2289 without the MD4 set (MD4 support was removed from
 * OpenBSD base in March 2014) and with the addition of an RIPEMD-160 set.
 */

#include <stdio.h>
#include <string.h>
#include "skey.h"

struct regRes {
	char *algo, *zero, *one, *nine;
	};

struct regPass {
	char *passphrase, *seed;
	struct regRes res[4];
	} regPass[] = {
		{ "This is a test.", "TeSt", { 
			{ "md5", "9E876134D90499DD", "7965E05436F5029F", "50FE1962C4965880" },
			{ "rmd160","3A1BFB10A64B4CCD", "39D56BF655E65DE7", "42F84BA862941033" },
			{ "sha1","BB9E6AE1979D8FF4", "63D936639734385B", "87FEC7768B73CCF9" },
			{ NULL } } },
		{ "AbCdEfGhIjK", "alpha1", { 
			{ "md5", "87066DD9644BF206", "7CD34C1040ADD14B", "5AA37A81F212146C" },
			{ "rmd160","726EDD1BB5DB3642", "46A231C501A1D2CE", "848664EF3A300CC9" },
			{ "sha1","AD85F658EBE383C9", "D07CE229B5CF119B", "27BC71035AAF3DC6" },
			{ NULL } } },
		{ "OTP's are good", "correct", { 
			{ "md5", "F205753943DE4CF9", "DDCDAC956F234937", "B203E28FA525BE47" },
			{ "rmd160","F90D03CC969208C8", "B6F5D25A08A90009", "C890C1F05018BA5F" },
			{ "sha1","D51F3E99BF8E6F0B", "82AEB52D943774E4", "4F296A74FE1567EC" },
			{ NULL } } },
		{ NULL }
	};

int
main(int argc, char *argv[])
{
	char data[16], prn[64];
	struct regPass *rp;
	int i = 0;
	int errors = 0;
	int j;

	if (strcmp(skey_get_algorithm(), "md5") != 0) {
		errors++;
		printf("default algorithm is not md5\n");
	}

	if (skey_set_algorithm("md4") != NULL) {
		errors++;
		printf("accepted unsupported algorithm md4\n");
	}

	for(rp = regPass; rp->passphrase; rp++) {
		struct regRes *rr;

		i++;
		for(rr = rp->res; rr->algo; rr++) {
			if (skey_set_algorithm(rr->algo) == NULL) {
				errors++;
				printf("Set %d: %s algorithm is not supported\n",
				    i, rr->algo);
				continue;
			}

			if (strcmp(skey_get_algorithm(), rr->algo) != 0) {
				errors++;
				printf("Set %d: unable to set algorithm to %s\n",
				    i, rr->algo);
				continue;
			}

			keycrunch(data, rp->seed, rp->passphrase);
			btoa8(prn, data);

			if(strcasecmp(prn, rr->zero)) {
				errors++;
				printf("Set %d, round 0, %s: Expected %s and got %s\n",
				    i, rr->algo, rr->zero, prn);
			}

			f(data);
			btoa8(prn, data);

			if(strcasecmp(prn, rr->one)) {
				errors++;
				printf("Set %d, round 1, %s: Expected %s and got %s\n",
				    i, rr->algo, rr->one, prn);
			}

			for(j=1; j<99; j++)
				f(data);
			btoa8(prn, data);

			if(strcasecmp(prn, rr->nine)) {
				errors++;
				printf("Set %d, round 99, %s: Expected %s and got %s\n",
				    i, rr->algo, rr->nine, prn);
			}
		}
	}

	printf("%d errors\n", errors);
	return(errors ? 1 : 0);
}
