/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Pawel Jakub Dawidek under sponsorship from
 * the FreeBSD Foundation.
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

/*
 * Note that this file is compiled into the kernel and into libc.
 */

#include <sys/types.h>
#include <sys/capsicum.h>

#ifdef _KERNEL
#include <sys/systm.h>
#include <sys/kernel.h>
#include <machine/stdarg.h>
#else	/* !_KERNEL */
#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#endif

#ifdef _KERNEL
#define	assert(exp)	KASSERT((exp), ("%s:%u", __func__, __LINE__))
__read_mostly cap_rights_t cap_accept_rights;
__read_mostly cap_rights_t cap_bind_rights;
__read_mostly cap_rights_t cap_chflags_rights;
__read_mostly cap_rights_t cap_connect_rights;
__read_mostly cap_rights_t cap_event_rights;
__read_mostly cap_rights_t cap_fchdir_rights;
__read_mostly cap_rights_t cap_fchflags_rights;
__read_mostly cap_rights_t cap_fchmod_rights;
__read_mostly cap_rights_t cap_fchown_rights;
__read_mostly cap_rights_t cap_fcntl_rights;
__read_mostly cap_rights_t cap_fexecve_rights;
__read_mostly cap_rights_t cap_flock_rights;
__read_mostly cap_rights_t cap_fpathconf_rights;
__read_mostly cap_rights_t cap_fstat_rights;
__read_mostly cap_rights_t cap_fstatfs_rights;
__read_mostly cap_rights_t cap_fsync_rights;
__read_mostly cap_rights_t cap_ftruncate_rights;
__read_mostly cap_rights_t cap_futimes_rights;
__read_mostly cap_rights_t cap_getpeername_rights;
__read_mostly cap_rights_t cap_getsockopt_rights;
__read_mostly cap_rights_t cap_getsockname_rights;
__read_mostly cap_rights_t cap_ioctl_rights;
__read_mostly cap_rights_t cap_listen_rights;
__read_mostly cap_rights_t cap_linkat_source_rights;
__read_mostly cap_rights_t cap_linkat_target_rights;
__read_mostly cap_rights_t cap_mmap_rights;
__read_mostly cap_rights_t cap_mkdirat_rights;
__read_mostly cap_rights_t cap_mkfifoat_rights;
__read_mostly cap_rights_t cap_mknodat_rights;
__read_mostly cap_rights_t cap_pdgetpid_rights;
__read_mostly cap_rights_t cap_pdkill_rights;
__read_mostly cap_rights_t cap_pread_rights;
__read_mostly cap_rights_t cap_pwrite_rights;
__read_mostly cap_rights_t cap_read_rights;
__read_mostly cap_rights_t cap_recv_rights;
__read_mostly cap_rights_t cap_renameat_source_rights;
__read_mostly cap_rights_t cap_renameat_target_rights;
__read_mostly cap_rights_t cap_seek_rights;
__read_mostly cap_rights_t cap_send_rights;
__read_mostly cap_rights_t cap_send_connect_rights;
__read_mostly cap_rights_t cap_setsockopt_rights;
__read_mostly cap_rights_t cap_shutdown_rights;
__read_mostly cap_rights_t cap_symlinkat_rights;
__read_mostly cap_rights_t cap_unlinkat_rights;
__read_mostly cap_rights_t cap_write_rights;
__read_mostly cap_rights_t cap_no_rights;

static void
__cap_rights_sysinit1(void *arg)
{
	cap_rights_init(&cap_accept_rights, CAP_ACCEPT);
	cap_rights_init(&cap_bind_rights, CAP_BIND);
	cap_rights_init(&cap_connect_rights, CAP_CONNECT);
	cap_rights_init(&cap_event_rights, CAP_EVENT);
	cap_rights_init(&cap_fchdir_rights, CAP_FCHDIR);
	cap_rights_init(&cap_fchflags_rights, CAP_FCHFLAGS);
	cap_rights_init(&cap_fchmod_rights, CAP_FCHMOD);
	cap_rights_init(&cap_fchown_rights, CAP_FCHOWN);
	cap_rights_init(&cap_fcntl_rights, CAP_FCNTL);
	cap_rights_init(&cap_fexecve_rights, CAP_FEXECVE);
	cap_rights_init(&cap_flock_rights, CAP_FLOCK);
	cap_rights_init(&cap_fpathconf_rights, CAP_FPATHCONF);
	cap_rights_init(&cap_fstat_rights, CAP_FSTAT);
	cap_rights_init(&cap_fstatfs_rights, CAP_FSTATFS);
	cap_rights_init(&cap_fsync_rights, CAP_FSYNC);
	cap_rights_init(&cap_ftruncate_rights, CAP_FTRUNCATE);
	cap_rights_init(&cap_futimes_rights, CAP_FUTIMES);
	cap_rights_init(&cap_getpeername_rights, CAP_GETPEERNAME);
	cap_rights_init(&cap_getsockname_rights, CAP_GETSOCKNAME);
	cap_rights_init(&cap_getsockopt_rights, CAP_GETSOCKOPT);
	cap_rights_init(&cap_ioctl_rights, CAP_IOCTL);
	cap_rights_init(&cap_linkat_source_rights, CAP_LINKAT_SOURCE);
	cap_rights_init(&cap_linkat_target_rights, CAP_LINKAT_TARGET);
	cap_rights_init(&cap_listen_rights, CAP_LISTEN);
	cap_rights_init(&cap_mkdirat_rights, CAP_MKDIRAT);
	cap_rights_init(&cap_mkfifoat_rights, CAP_MKFIFOAT);
	cap_rights_init(&cap_mknodat_rights, CAP_MKNODAT);
	cap_rights_init(&cap_mmap_rights, CAP_MMAP);
	cap_rights_init(&cap_pdgetpid_rights, CAP_PDGETPID);
	cap_rights_init(&cap_pdkill_rights, CAP_PDKILL);
	cap_rights_init(&cap_pread_rights, CAP_PREAD);
	cap_rights_init(&cap_pwrite_rights, CAP_PWRITE);
	cap_rights_init(&cap_read_rights, CAP_READ);
	cap_rights_init(&cap_recv_rights, CAP_RECV);
	cap_rights_init(&cap_renameat_source_rights, CAP_RENAMEAT_SOURCE);
	cap_rights_init(&cap_renameat_target_rights, CAP_RENAMEAT_TARGET);
	cap_rights_init(&cap_seek_rights, CAP_SEEK);
	cap_rights_init(&cap_send_rights, CAP_SEND);
	cap_rights_init(&cap_send_connect_rights, CAP_SEND, CAP_CONNECT);
	cap_rights_init(&cap_setsockopt_rights, CAP_SETSOCKOPT);
	cap_rights_init(&cap_shutdown_rights, CAP_SHUTDOWN);
	cap_rights_init(&cap_symlinkat_rights, CAP_SYMLINKAT);
	cap_rights_init(&cap_unlinkat_rights, CAP_UNLINKAT);
	cap_rights_init(&cap_write_rights, CAP_WRITE);
	cap_rights_init(&cap_no_rights);
}
SYSINIT(cap_rights1_sysinit, SI_SUB_COPYRIGHT, SI_ORDER_ANY,	\
		    __cap_rights_sysinit1, NULL);

#endif

#define	CAPARSIZE_MIN	(CAP_RIGHTS_VERSION_00 + 2)
#define	CAPARSIZE_MAX	(CAP_RIGHTS_VERSION + 2)

static __inline int
right_to_index(uint64_t right)
{
	static const int bit2idx[] = {
		-1, 0, 1, -1, 2, -1, -1, -1, 3, -1, -1, -1, -1, -1, -1, -1,
		4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
	};
	int idx;

	idx = CAPIDXBIT(right);
	assert(idx >= 0 && idx < sizeof(bit2idx) / sizeof(bit2idx[0]));
	return (bit2idx[idx]);
}

static void
cap_rights_vset(cap_rights_t *rights, va_list ap)
{
	uint64_t right;
	int i, n __unused;

	assert(CAPVER(rights) == CAP_RIGHTS_VERSION_00);

	n = CAPARSIZE(rights);
	assert(n >= CAPARSIZE_MIN && n <= CAPARSIZE_MAX);

	for (;;) {
		right = (uint64_t)va_arg(ap, unsigned long long);
		if (right == 0)
			break;
		assert(CAPRVER(right) == 0);
		i = right_to_index(right);
		assert(i >= 0);
		assert(i < n);
		assert(CAPIDXBIT(rights->cr_rights[i]) == CAPIDXBIT(right));
		rights->cr_rights[i] |= right;
		assert(CAPIDXBIT(rights->cr_rights[i]) == CAPIDXBIT(right));
	}
}

static void
cap_rights_vclear(cap_rights_t *rights, va_list ap)
{
	uint64_t right;
	int i, n __unused;

	assert(CAPVER(rights) == CAP_RIGHTS_VERSION_00);

	n = CAPARSIZE(rights);
	assert(n >= CAPARSIZE_MIN && n <= CAPARSIZE_MAX);

	for (;;) {
		right = (uint64_t)va_arg(ap, unsigned long long);
		if (right == 0)
			break;
		assert(CAPRVER(right) == 0);
		i = right_to_index(right);
		assert(i >= 0);
		assert(i < n);
		assert(CAPIDXBIT(rights->cr_rights[i]) == CAPIDXBIT(right));
		rights->cr_rights[i] &= ~(right & 0x01FFFFFFFFFFFFFFULL);
		assert(CAPIDXBIT(rights->cr_rights[i]) == CAPIDXBIT(right));
	}
}

static bool
cap_rights_is_vset(const cap_rights_t *rights, va_list ap)
{
	uint64_t right;
	int i, n __unused;

	assert(CAPVER(rights) == CAP_RIGHTS_VERSION_00);

	n = CAPARSIZE(rights);
	assert(n >= CAPARSIZE_MIN && n <= CAPARSIZE_MAX);

	for (;;) {
		right = (uint64_t)va_arg(ap, unsigned long long);
		if (right == 0)
			break;
		assert(CAPRVER(right) == 0);
		i = right_to_index(right);
		assert(i >= 0);
		assert(i < n);
		assert(CAPIDXBIT(rights->cr_rights[i]) == CAPIDXBIT(right));
		if ((rights->cr_rights[i] & right) != right)
			return (false);
	}

	return (true);
}

void
__cap_rights_sysinit(void *arg)
{
	struct cap_rights_init_args *cria = arg;
	cap_rights_t *rights = cria->cria_rights;

	__cap_rights_init(CAP_RIGHTS_VERSION, rights, cria->cria_value1,
       cria->cria_value2, cria->cria_value3, cria->cria_value4, 0ULL);
}

cap_rights_t *
__cap_rights_init(int version, cap_rights_t *rights, ...)
{
	unsigned int n __unused;
	va_list ap;

	assert(version == CAP_RIGHTS_VERSION_00);

	n = version + 2;
	assert(n >= CAPARSIZE_MIN && n <= CAPARSIZE_MAX);
	CAP_NONE(rights);
	va_start(ap, rights);
	cap_rights_vset(rights, ap);
	va_end(ap);

	return (rights);
}

cap_rights_t *
__cap_rights_set(cap_rights_t *rights, ...)
{
	va_list ap;

	assert(CAPVER(rights) == CAP_RIGHTS_VERSION_00);

	va_start(ap, rights);
	cap_rights_vset(rights, ap);
	va_end(ap);

	return (rights);
}

cap_rights_t *
__cap_rights_clear(cap_rights_t *rights, ...)
{
	va_list ap;

	assert(CAPVER(rights) == CAP_RIGHTS_VERSION_00);

	va_start(ap, rights);
	cap_rights_vclear(rights, ap);
	va_end(ap);

	return (rights);
}

bool
__cap_rights_is_set(const cap_rights_t *rights, ...)
{
	va_list ap;
	bool ret;

	assert(CAPVER(rights) == CAP_RIGHTS_VERSION_00);

	va_start(ap, rights);
	ret = cap_rights_is_vset(rights, ap);
	va_end(ap);

	return (ret);
}

bool
cap_rights_is_valid(const cap_rights_t *rights)
{
	cap_rights_t allrights;
	int i, j;

	if (CAPVER(rights) != CAP_RIGHTS_VERSION_00)
		return (false);
	if (CAPARSIZE(rights) < CAPARSIZE_MIN ||
	    CAPARSIZE(rights) > CAPARSIZE_MAX) {
		return (false);
	}
	CAP_ALL(&allrights);
	if (!cap_rights_contains(&allrights, rights))
		return (false);
	for (i = 0; i < CAPARSIZE(rights); i++) {
		j = right_to_index(rights->cr_rights[i]);
		if (i != j)
			return (false);
		if (i > 0) {
			if (CAPRVER(rights->cr_rights[i]) != 0)
				return (false);
		}
	}

	return (true);
}

cap_rights_t *
cap_rights_merge(cap_rights_t *dst, const cap_rights_t *src)
{
	unsigned int i, n;

	assert(CAPVER(dst) == CAP_RIGHTS_VERSION_00);
	assert(CAPVER(src) == CAP_RIGHTS_VERSION_00);
	assert(CAPVER(dst) == CAPVER(src));
	assert(cap_rights_is_valid(src));
	assert(cap_rights_is_valid(dst));

	n = CAPARSIZE(dst);
	assert(n >= CAPARSIZE_MIN && n <= CAPARSIZE_MAX);

	for (i = 0; i < n; i++)
		dst->cr_rights[i] |= src->cr_rights[i];

	assert(cap_rights_is_valid(src));
	assert(cap_rights_is_valid(dst));

	return (dst);
}

cap_rights_t *
cap_rights_remove(cap_rights_t *dst, const cap_rights_t *src)
{
	unsigned int i, n;

	assert(CAPVER(dst) == CAP_RIGHTS_VERSION_00);
	assert(CAPVER(src) == CAP_RIGHTS_VERSION_00);
	assert(CAPVER(dst) == CAPVER(src));
	assert(cap_rights_is_valid(src));
	assert(cap_rights_is_valid(dst));

	n = CAPARSIZE(dst);
	assert(n >= CAPARSIZE_MIN && n <= CAPARSIZE_MAX);

	for (i = 0; i < n; i++) {
		dst->cr_rights[i] &=
		    ~(src->cr_rights[i] & 0x01FFFFFFFFFFFFFFULL);
	}

	assert(cap_rights_is_valid(src));
	assert(cap_rights_is_valid(dst));

	return (dst);
}

bool
cap_rights_contains(const cap_rights_t *big, const cap_rights_t *little)
{
	unsigned int i, n;

	assert(CAPVER(big) == CAP_RIGHTS_VERSION_00);
	assert(CAPVER(little) == CAP_RIGHTS_VERSION_00);
	assert(CAPVER(big) == CAPVER(little));

	n = CAPARSIZE(big);
	assert(n >= CAPARSIZE_MIN && n <= CAPARSIZE_MAX);

	for (i = 0; i < n; i++) {
		if ((big->cr_rights[i] & little->cr_rights[i]) !=
		    little->cr_rights[i]) {
			return (false);
		}
	}

	return (true);
}
