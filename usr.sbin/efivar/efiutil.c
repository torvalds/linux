/*-
 * Copyright (c) 2017-2019 Netflix, Inc.
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

#include <ctype.h>
#include <efivar.h>
#include <efivar-dp.h>
#include <err.h>
#include <errno.h>
#include <getopt.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "efiutil.h"
#include "efichar.h"
#include <efivar-dp.h>

/*
 * Dump the data as ASCII data, which is a pretty
 * printed form 
 */
void
asciidump(uint8_t *data, size_t datalen)
{
	size_t i;
	int len;

	len = 0;
	for (i = 0; i < datalen; i++) {
		if (isprint(data[i])) {
			len++;
			if (len > 80) {
				len = 0;
				printf("\n");
			}
			printf("%c", data[i]);
		} else {
			len +=3;
			if (len > 80) {
				len = 0;
				printf("\n");
			}
			printf("%%%02x", data[i]);
		}
	}
	printf("\n");
}

void
utf8dump(uint8_t *data, size_t datalen)
{
	char *utf8 = NULL;
	efi_char *ucs2;

	/*
	 * NUL terminate the string. Not all strings need it, but some
	 * do and an extra NUL won't change what's printed.
	 */
	ucs2 = malloc(datalen + sizeof(efi_char));
	memcpy(ucs2, data, datalen);
	ucs2[datalen / sizeof(efi_char)] = 0;
	ucs2_to_utf8(ucs2, &utf8);
	printf("%s\n", utf8);
	free(utf8);
	free(ucs2);
}

void
hexdump(uint8_t *data, size_t datalen)
{
	size_t i;

	for (i = 0; i < datalen; i++) {
		if (i % 16 == 0) {
			if (i != 0)
				printf("\n");
			printf("%04x: ", (int)i);
		}
		printf("%02x ", data[i]);
	}
	printf("\n");
}

void
bindump(uint8_t *data, size_t datalen)
{
	write(1, data, datalen);
}

#define LOAD_OPTION_ACTIVE 1

#define SIZE(dp, edp) (size_t)((intptr_t)(void *)edp - (intptr_t)(void *)dp)

void
efi_print_load_option(uint8_t *data, size_t datalen, int Aflag, int bflag, int uflag)
{
	char *dev, *relpath, *abspath;
	uint8_t *ep = data + datalen;
	uint8_t *walker = data;
	uint32_t attr;
	uint16_t fplen;
	efi_char *descr;
	efidp dp, edp;
	char *str = NULL;
	char buf[1024];
	int len;
	void *opt;
	int optlen;
	int rv;

	if (datalen < sizeof(attr) + sizeof(fplen) + sizeof(efi_char))
		return;
	// First 4 bytes are attribute flags
	attr = le32dec(walker);
	walker += sizeof(attr);
	// Next two bytes are length of the file paths
	fplen = le16dec(walker);
	walker += sizeof(fplen);
	// Next we have a 0 terminated UCS2 string that we know to be aligned
	descr = (efi_char *)(intptr_t)(void *)walker;
	len = ucs2len(descr); // XXX need to sanity check that len < (datalen - (ep - walker) / 2)
	walker += (len + 1) * sizeof(efi_char);
	if (walker > ep)
		return;
	// Now we have fplen bytes worth of file path stuff
	dp = (efidp)walker;
	walker += fplen;
	if (walker > ep)
		return;
	edp = (efidp)walker;
	// Everything left is the binary option args
	opt = walker;
	optlen = ep - walker;
	// We got to here, everything is good
	printf("%c ", attr & LOAD_OPTION_ACTIVE ? '*' : ' ');
	ucs2_to_utf8(descr, &str);
	printf("%s", str);
	free(str);
	while (dp < edp && SIZE(dp, edp) > sizeof(efidp_header)) {
		efidp_format_device_path(buf, sizeof(buf), dp, SIZE(dp, edp));
		rv = efivar_device_path_to_unix_path(dp, &dev, &relpath, &abspath);
		dp = (efidp)((char *)dp + efidp_size(dp));
		printf(" %s\n", buf);
		if (rv == 0) {
			printf("      %*s:%s\n", len + (int)strlen(dev), dev, relpath);
			free(dev);
			free(relpath);
			free(abspath);
		}
	}
	if (optlen == 0)
		return;
	printf("Options: ");
	if (Aflag)
		asciidump(opt, optlen);
	else if (bflag)
		bindump(opt, optlen);
	else if (uflag)
		utf8dump(opt, optlen);
	else
		hexdump(opt, optlen);
}
