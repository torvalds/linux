/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010-2012 Semihalf.
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

#ifndef	_LIBNANDFS_NANDFS_H
#define	_LIBNANDFS_NANDFS_H

struct nandfs {
	struct nandfs_fsdata		n_fsdata;
	struct nandfs_super_block	n_sb;
	char				n_ioc[MNAMELEN];
	char				n_dev[MNAMELEN];
	int				n_iocfd;
	int				n_devfd;
	int				n_flags;
	char				n_errmsg[120];
};

int nandfs_iserror(struct nandfs *);
const char *nandfs_errmsg(struct nandfs *);

void nandfs_init(struct nandfs *, const char *);
void nandfs_destroy(struct nandfs *);

const char *nandfs_dev(struct nandfs *);

int nandfs_open(struct nandfs *);
void nandfs_close(struct nandfs *);

int nandfs_get_cpstat(struct nandfs *, struct nandfs_cpstat *);

ssize_t nandfs_get_cp(struct nandfs *, uint64_t,
    struct nandfs_cpinfo *, size_t);

ssize_t nandfs_get_snap(struct nandfs *, uint64_t,
    struct nandfs_cpinfo *, size_t);

int nandfs_make_snap(struct nandfs *, uint64_t *);
int nandfs_delete_snap(struct nandfs *, uint64_t);

#endif	/* _LIBNANDFS_NANDFS_H */
