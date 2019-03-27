/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2018 The FreeBSD Foundation.
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
 * $FreeBSD$
 */

#include <err.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const size_t bufsize = 65536;

/* SDM vol 3 9.11.1 Intel microcode header. */
struct microcode_update_header {
	uint32_t header_version;
	uint32_t update_revision;
	uint32_t date;			/* BCD mmddyyyy */
	uint32_t processor_signature;
	uint32_t checksum;		/* Over update data and header */
	uint32_t loader_revision;
	uint32_t processor_flags;
	uint32_t data_size;
	uint32_t total_size;
	uint32_t reserved[3];
};

/*
 * SDM vol 2A CPUID EAX = 01h Returns Model, Family, Stepping Information.
 * Caller must free the returned string.
 */

static char *
format_signature(uint32_t signature)
{
	char *buf;
	unsigned family, model, stepping;

	family = (signature & 0xf00) >> 8;
	model = (signature & 0xf0) >> 4;
	stepping = signature & 0xf;
	if (family == 0x06 || family == 0x0f)
		model += (signature & 0xf0000) >> 12;
	if (family == 0x0f)
		family += (signature & 0xff00000) >> 20;
	asprintf(&buf, "%02x-%02x-%02x", family, model, stepping);
	if (buf == NULL)
		err(1, "asprintf");
	return (buf);
}

static void
dump_header(const struct microcode_update_header *hdr)
{
	char *sig_str;
	int i;
	bool platformid_printed;

	sig_str = format_signature(hdr->processor_signature);
	printf("header version\t0x%x\n", hdr->header_version);
	printf("revision\t0x%x\n", hdr->update_revision);
	printf("date\t\t0x%x\t%04x-%02x-%02x\n", hdr->date,
	    hdr->date & 0xffff, (hdr->date & 0xff000000) >> 24,
	    (hdr->date & 0xff0000) >> 16);
	printf("signature\t0x%x\t\t%s\n", hdr->processor_signature, sig_str);
	printf("checksum\t0x%x\n", hdr->checksum);
	printf("loader revision\t0x%x\n", hdr->loader_revision);
	printf("processor flags\t0x%x", hdr->processor_flags);
	platformid_printed = false;
	for (i = 0; i < 8; i++) {
		if (hdr->processor_flags & 1 << i) {
			printf("%s%d", platformid_printed ? ", " : "\t\t", i);
			platformid_printed = true;
		}
	}
	printf("\n");
	printf("datasize\t0x%x\t\t0x%x\n", hdr->data_size,
	    hdr->data_size != 0 ? hdr->data_size : 2000);
	printf("size\t\t0x%x\t\t0x%x\n", hdr->total_size,
	    hdr->total_size != 0 ? hdr->total_size : 2048);
	free(sig_str);
}

static void
usage(void)
{

	printf("ucode-split [-nv] microcode_file\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	struct microcode_update_header hdr;
	char *buf, *output_file, *sig_str;
	size_t len, resid;
	ssize_t rv;
	int c, ifd, ofd;
	bool nflag, vflag;

	nflag = vflag = false;
	while ((c = getopt(argc, argv, "nv")) != -1) {
		switch (c) {
		case 'n':
			nflag = true;
			break;
		case 'v':
			vflag = true;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 1)
		usage();

	ifd = open(argv[0], O_RDONLY);
	if (ifd < 0)
		err(1, "open");

	buf = malloc(bufsize);
	if (buf == NULL)
		err(1, "malloc");

	for (;;) {
		/* Read header. */
		rv = read(ifd, &hdr, sizeof(hdr));
		if (rv < 0) {
			err(1, "read");
		} else if (rv == 0) {
			break;
		} else if (rv < (ssize_t)sizeof(hdr)) {
			errx(1, "invalid microcode header");
		}
		if (hdr.header_version != 1)
			errx(1, "invalid header version");

		if (vflag)
			dump_header(&hdr);

		resid = (hdr.total_size != 0 ? hdr.total_size : 2048) -
		    sizeof(hdr);
		if (resid > 1 << 24) /* Arbitrary chosen maximum size. */
			errx(1, "header total_size too large");

		if (nflag) {
			if (lseek(ifd, resid, SEEK_CUR) == -1)
				err(1, "lseek");
			printf("\n");
		} else {
			sig_str = format_signature(hdr.processor_signature);
			asprintf(&output_file, "%s.%02x", sig_str,
			    hdr.processor_flags & 0xff);
			free(sig_str);
			if (output_file == NULL)
				err(1, "asprintf");
			ofd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC,
			    0600);
			if (ofd < 0)
				err(1, "open");
	
			/* Write header. */
			rv = write(ofd, &hdr, sizeof(hdr));
			if (rv < (ssize_t)sizeof(hdr))
				err(1, "write");
	
			/* Copy data. */
			while (resid > 0) {
				len = resid < bufsize ? resid : bufsize;
				rv = read(ifd, buf, len);
				if (rv < 0)
					err(1, "read");
				else if (rv < (ssize_t)len)
					errx(1, "truncated microcode data");
				if (write(ofd, buf, len) < (ssize_t)len)
					err(1, "write");
				resid -= len;
			}
			if (vflag)
				printf("written to %s\n\n", output_file);
			close(ofd);
			free(output_file);
		}
	}
}
