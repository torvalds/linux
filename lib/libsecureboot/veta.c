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

/**
 * @file veta.c - add to trust anchors
 *
 */

#define NEED_BRSSL_H
#include "libsecureboot-priv.h"
#include <brssl.h>
#include <sys/stat.h>
#include <dirent.h>

#ifdef VE_OPENPGP_SUPPORT
#include "openpgp/packet.h"
#endif

/**
 * @brief add trust anchors from a file
 *
 * The file might contain X.509 certs
 * or OpenPGP public key
 */
static size_t
trust_file_add(const char *trust)
{
	br_x509_certificate *xcs;
	size_t num;

	xcs = read_certificates(trust, &num);
	if (xcs) {
		num = ve_trust_anchors_add(xcs, num);
	}
#ifdef VE_OPENPGP_SUPPORT
	else if (load_key_file(trust)) {
		num = 1;
	}
#endif
	return (num);
}

/**
 * @brief add trust anchors from a directory
 *
 * Pass each file in directory to trust_file_add
 */
static size_t
trust_dir_add(const char *trust)
{
	char fbuf[MAXPATHLEN];
	DIR *dh;
	struct dirent *de;
	struct stat st;
	ssize_t sz;
	size_t num;

	if (!(dh = opendir(trust)))
		return (0);
	for (num = 0, de = readdir(dh); de; de = readdir(dh)) {
		if (de->d_name[0] == '.')
			continue;
		sz = snprintf(fbuf, sizeof(fbuf), "%s/%s", trust, de->d_name);
		if (sz >= (ssize_t)sizeof(fbuf))
			continue;
		if (stat(fbuf, &st) < 0 || S_ISDIR(st.st_mode))
			continue;
		num += trust_file_add(fbuf);
	}
	closedir(dh);
	return (num);
}

/**
 * @brief add trust anchors
 */
int
ve_trust_add(const char *trust)
{
	struct stat st;

	if (stat(trust, &st) < 0)
		return (-1);
	if (S_ISDIR(st.st_mode))
		return (trust_dir_add(trust));
	return (trust_file_add(trust));
}
