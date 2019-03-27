/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Robert N. M. Watson
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
#include <sys/sysctl.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <kvm.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "ddb.h"

/*
 * Interface with the ddb(4) capture buffer of a live kernel using sysctl, or
 * for a crash dump using libkvm.
 */
#define	SYSCTL_DDB_CAPTURE_BUFOFF	"debug.ddb.capture.bufoff"
#define	SYSCTL_DDB_CAPTURE_BUFSIZE	"debug.ddb.capture.bufsize"
#define	SYSCTL_DDB_CAPTURE_MAXBUFSIZE	"debug.ddb.capture.maxbufsize"
#define	SYSCTL_DDB_CAPTURE_DATA		"debug.ddb.capture.data"
#define	SYSCTL_DDB_CAPTURE_INPROGRESS	"debug.ddb.capture.inprogress"

static struct nlist namelist[] = {
#define X_DB_CAPTURE_BUF	0
	{ .n_name = "_db_capture_buf" },
#define X_DB_CAPTURE_BUFSIZE	1
	{ .n_name = "_db_capture_bufsize" },
#define X_DB_CAPTURE_MAXBUFSIZE	2
	{ .n_name = "_db_capture_maxbufsize" },
#define X_DB_CAPTURE_BUFOFF	3
	{ .n_name = "_db_capture_bufoff" },
#define	X_DB_CAPTURE_INPROGRESS	4
	{ .n_name = "_db_capture_inprogress" },
	{ .n_name = "" },
};

static int
kread(kvm_t *kvm, void *kvm_pointer, void *address, size_t size,
    size_t offset)
{
	ssize_t ret;

	ret = kvm_read(kvm, (unsigned long)kvm_pointer + offset, address,
	    size);
	if (ret < 0 || (size_t)ret != size)
		return (-1);
	return (0);
}

static int
kread_symbol(kvm_t *kvm, int read_index, void *address, size_t size,
    size_t offset)
{
	ssize_t ret;

	ret = kvm_read(kvm, namelist[read_index].n_value + offset, address, size);
	if (ret < 0 || (size_t)ret != size)
		return (-1);
	return (0);
}

static void
ddb_capture_print_kvm(kvm_t *kvm)
{
	u_int db_capture_bufoff;
	char *buffer, *db_capture_buf;

	if (kread_symbol(kvm, X_DB_CAPTURE_BUF, &db_capture_buf,
	    sizeof(db_capture_buf), 0) < 0)
		errx(-1, "kvm: unable to read db_capture_buf");

	if (kread_symbol(kvm, X_DB_CAPTURE_BUFOFF, &db_capture_bufoff,
	    sizeof(db_capture_bufoff), 0) < 0)
		errx(-1, "kvm: unable to read db_capture_bufoff");

	buffer = malloc(db_capture_bufoff + 1);
	if (buffer == NULL)
		err(-1, "malloc: db_capture_bufoff (%u)",
		    db_capture_bufoff);
	bzero(buffer, db_capture_bufoff + 1);

	if (kread(kvm, db_capture_buf, buffer, db_capture_bufoff, 0) < 0)
		errx(-1, "kvm: unable to read buffer");

	printf("%s\n", buffer);
	free(buffer);
}

static void
ddb_capture_print_sysctl(void)
{
	size_t buflen, len;
	char *buffer;
	int ret;

repeat:
	if (sysctlbyname(SYSCTL_DDB_CAPTURE_DATA, NULL, &buflen, NULL, 0) < 0)
		err(EX_OSERR, "sysctl: %s", SYSCTL_DDB_CAPTURE_DATA);
	if (buflen == 0)
		return;
	buffer = malloc(buflen);
	if (buffer == NULL)
		err(EX_OSERR, "malloc");
	bzero(buffer, buflen);
	len = buflen;
	ret = sysctlbyname(SYSCTL_DDB_CAPTURE_DATA, buffer, &len, NULL, 0);
	if (ret < 0 && errno != ENOMEM)
		err(EX_OSERR, "sysctl: %s", SYSCTL_DDB_CAPTURE_DATA);
	if (ret < 0) {
		free(buffer);
		goto repeat;
	}

	printf("%s\n", buffer);
	free(buffer);
}

static void
ddb_capture_status_kvm(kvm_t *kvm)
{
	u_int db_capture_bufoff, db_capture_bufsize, db_capture_inprogress;

	if (kread_symbol(kvm, X_DB_CAPTURE_BUFOFF, &db_capture_bufoff,
	    sizeof(db_capture_bufoff), 0) < 0)
		errx(-1, "kvm: unable to read db_capture_bufoff");
	if (kread_symbol(kvm, X_DB_CAPTURE_BUFSIZE, &db_capture_bufsize,
	    sizeof(db_capture_bufsize), 0) < 0)
		errx(-1, "kvm: unable to read db_capture_bufsize");
	if (kread_symbol(kvm, X_DB_CAPTURE_INPROGRESS,
	    &db_capture_inprogress, sizeof(db_capture_inprogress), 0) < 0)
		err(-1, "kvm: unable to read db_capture_inprogress");
	printf("%u/%u bytes used\n", db_capture_bufoff, db_capture_bufsize);
	if (db_capture_inprogress)
		printf("capture is on\n");
	else
		printf("capture is off\n");

}

static void
ddb_capture_status_sysctl(void)
{
	u_int db_capture_bufoff, db_capture_bufsize, db_capture_inprogress;
	size_t len;

	len = sizeof(db_capture_bufoff);
	if (sysctlbyname(SYSCTL_DDB_CAPTURE_BUFOFF, &db_capture_bufoff, &len,
	    NULL, 0) < 0)
		err(EX_OSERR, "sysctl: %s", SYSCTL_DDB_CAPTURE_BUFOFF);
	len = sizeof(db_capture_bufoff);
	if (sysctlbyname(SYSCTL_DDB_CAPTURE_BUFSIZE, &db_capture_bufsize,
	    &len, NULL, 0) < 0)
		err(EX_OSERR, "sysctl: %s", SYSCTL_DDB_CAPTURE_BUFSIZE);
	len = sizeof(db_capture_inprogress);
	if (sysctlbyname(SYSCTL_DDB_CAPTURE_INPROGRESS,
	    &db_capture_inprogress, &len, NULL, 0) < 0)
		err(EX_OSERR, "sysctl: %s", SYSCTL_DDB_CAPTURE_INPROGRESS);
	printf("%u/%u bytes used\n", db_capture_bufoff, db_capture_bufsize);
	if (db_capture_inprogress)
		printf("capture is on\n");
	else
		printf("capture is off\n");
}

void
ddb_capture(int argc, char *argv[])
{
	char *mflag, *nflag, errbuf[_POSIX2_LINE_MAX];
	kvm_t *kvm;
	int ch;

	mflag = NULL;
	nflag = NULL;
	kvm = NULL;
	while ((ch = getopt(argc, argv, "M:N:")) != -1) {
		switch (ch) {
		case 'M':
			mflag = optarg;
			break;

		case 'N':
			nflag = optarg;
			break;

		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 1)
		usage();

	if (mflag != NULL) {
		kvm = kvm_openfiles(nflag, mflag, NULL, O_RDONLY, errbuf);
		if (kvm == NULL)
			errx(-1, "ddb_capture: kvm_openfiles: %s", errbuf);
		if (kvm_nlist(kvm, namelist) != 0)
			errx(-1, "ddb_capture: kvm_nlist");
	} else if (nflag != NULL)
		usage();
	if (strcmp(argv[0], "print") == 0) {
		if (kvm != NULL)
			ddb_capture_print_kvm(kvm);
		else
			ddb_capture_print_sysctl();
	} else if (strcmp(argv[0], "status") == 0) {
		if (kvm != NULL)
			ddb_capture_status_kvm(kvm);
		else
			ddb_capture_status_sysctl();
	} else
		usage();
}
