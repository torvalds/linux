/*-
 * Copyright (C) 2010 Gabor Kovesdan <gabor@FreeBSD.org>
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

#include <err.h>
#include <iconv.h>
#include <stdlib.h>
#include <string.h>

int
main(void)
{
	iconv_t cd;
	size_t inbytes, outbytes;
	char *str1 = "FOOBAR";
	const char *str2 = "FOOBAR";
	char ** in1;
	const char ** in2 = &str2;
	char *out1, *out2;

	inbytes = outbytes = strlen("FOOBAR");

	if ((cd = iconv_open("UTF-8", "ASCII")) == (iconv_t)-1)
		err(1, NULL);

	if ((out2 = malloc(inbytes)) == NULL)
		err(1, NULL);

	if (iconv(cd, in2, &inbytes, &out2, &outbytes) == -1)
		err(1, NULL);

	in1 = &str1;
	inbytes = outbytes = strlen("FOOBAR");

	if ((out1 = malloc(inbytes)) == NULL)
		err(1, NULL);

	if (iconv(cd, in1, &inbytes, &out1, &outbytes) == -1)
		err(1, NULL);

	return (EXIT_SUCCESS);

}
