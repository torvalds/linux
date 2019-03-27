/*-
 * Copyright 2015 John-Mark Gurney.
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
 *
 *	$FreeBSD$
 *
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

typedef uint32_t (*rndfun_t)();

uint32_t
randomwrap()
{
	return random();
}

struct {
	const char *name;
	rndfun_t rndfun;
} rndfuns[] = {
	{ "random", randomwrap },
	{ "arc4random", arc4random },
	{ NULL, NULL },
};

int
main(int argc, char *argv[])
{
	uint64_t vals[4] = {};
	uint64_t avg;
	unsigned int i;
	rndfun_t f;

	if (argc == 1)
		f = rndfuns[0].rndfun;
	else {
		for (i = 0; rndfuns[i].name != NULL; i++) {
			if (strcasecmp(rndfuns[i].name, argv[1]) == 0)
				break;
		}
		if (rndfuns[i].name == NULL)
			return 1;
		f = rndfuns[i].rndfun;
	}

	for (;;) {
		vals[f() % 4]++;
		if (((i++) % (4*1024*1024)) == 0) {
			avg = vals[0] + vals[1] + vals[2] + vals[3];
			avg /= 4;
			printf("%d: %ld %ld %ld %ld\n", i, vals[0] - avg, vals[1] - avg, vals[2] - avg, vals[3] - avg);
		}
	}
	return 0;
}
