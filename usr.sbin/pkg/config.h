/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Baptiste Daroussin <bapt@FreeBSD.org>
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
 * $FreeBSD$
 */

#ifndef _PKG_CONFIG_H
#define _PKG_CONFIG_H

#define _LOCALBASE "/usr/local"
#define URL_SCHEME_PREFIX "pkg+"

typedef enum {
	PACKAGESITE = 0,
	ABI,
	MIRROR_TYPE,
	ASSUME_ALWAYS_YES,
	SIGNATURE_TYPE,
	FINGERPRINTS,
	REPOS_DIR,
	PUBKEY,
	CONFIG_SIZE
} pkg_config_key;

typedef enum {
	PKG_CONFIG_STRING=0,
	PKG_CONFIG_BOOL,
	PKG_CONFIG_LIST,
} pkg_config_t;

typedef enum {
	CONFFILE_PKG=0,
	CONFFILE_REPO,
} pkg_conf_file_t;

int config_init(void);
void config_finish(void);
int config_string(pkg_config_key, const char **);
int config_bool(pkg_config_key, bool *);

#endif
