/*-
 * Copyright (c) 2018, Juniper Networks, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#define NEED_BRSSL_H
#include <libsecureboot.h>
#include <brssl.h>

#include "decode.h"

/**
 * @brief decode ascii armor
 *
 * once we get rid of the trailing checksum
 * we can treat as PEM.
 *
 * @sa rfc4880:6.2
 */
unsigned char *
dearmor(char *pem, size_t nbytes, size_t *len)
{
#ifdef USE_BEARSSL
	pem_object *po;
	size_t npo;
#else
	BIO *bp;
	char *name = NULL;
	char *header = NULL;
#endif
	unsigned char *data = NULL;
	char *cp;
	char *ep;

	/* we need to remove the Armor tail */
	if ((cp = strstr((char *)pem, "\n=")) &&
	    (ep = strstr(cp, "\n---"))) {
		memmove(cp, ep, nbytes - (size_t)(ep - pem));
		nbytes -= (size_t)(ep - cp);
		pem[nbytes] = '\0';
	}
#ifdef USE_BEARSSL
	/* we also need to remove any headers */
	if ((cp = strstr((char *)pem, "---\n")) &&
	    (ep = strstr(cp, "\n\n"))) {
		cp += 4;
		ep += 2;
		memmove(cp, ep, nbytes - (size_t)(ep - pem));
		nbytes -= (size_t)(ep - cp);
		pem[nbytes] = '\0';
	}
	if ((po = decode_pem(pem, nbytes, &npo))) {
		data = po->data;
		*len = po->data_len;
	}
#else
	if ((bp = BIO_new_mem_buf(pem, (int)nbytes))) {
		long llen = (long)nbytes;

		if (!PEM_read_bio(bp, &name, &header, &data, &llen))
			data = NULL;
		BIO_free(bp);
		*len = (size_t)llen;
	}
#endif
	return (data);
}

#ifdef MAIN_DEARMOR
#include <unistd.h>
#include <fcntl.h>
#include <err.h>

/*
 * Mostly a unit test.
 */
int
main(int argc, char *argv[])
{
	const char *infile, *outfile;
	unsigned char *data;
	size_t n, x;
	int fd;
	int o;

	infile = outfile = NULL;
	while ((o = getopt(argc, argv, "i:o:")) != -1) {
		switch (o) {
		case 'i':
			infile = optarg;
			break;
		case 'o':
			outfile = optarg;
			break;
		default:
			errx(1, "unknown option: -%c", o);
		}
	}
	if (!infile)
		errx(1, "need -i infile");
	if (outfile) {
		if ((fd = open(outfile, O_WRONLY|O_CREAT|O_TRUNC)) < 0)
			err(1, "cannot open %s", outfile);
	} else {
		fd = 1;			/* stdout */
	}
	data = read_file(infile, &n);
	if (!(data[0] & OPENPGP_TAG_ISTAG))
		data = dearmor(data, n, &n);
	for (x = 0; x < n; ) {
		o = write(fd, &data[x], (n - x));
		if (o < 0)
			err(1, "cannot write");
		x += o;
	}
	if (fd != 1)
		close(fd);
	free(data);
	return (0);
}
#endif
