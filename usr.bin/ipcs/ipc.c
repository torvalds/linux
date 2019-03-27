/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1994 SigmaSoft, Th. Lockert <tholo@sigmasoft.com>
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL
 * THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The split of ipcs.c into ipcs.c and ipc.c to accommodate the
 * changes in ipcrm.c was done by Edwin Groothuis <edwin@FreeBSD.org>
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/sysctl.h>
#define	_WANT_SYSVMSG_INTERNALS
#include <sys/msg.h>
#define	_WANT_SYSVSEM_INTERNALS
#include <sys/sem.h>
#define	_WANT_SYSVSHM_INTERNALS
#include <sys/shm.h>

#include <assert.h>
#include <err.h>
#include <kvm.h>
#include <nlist.h>
#include <stddef.h>

#include "ipc.h"

int	use_sysctl = 1;
struct semid_kernel	*sema;
struct seminfo		seminfo;
struct msginfo		msginfo;
struct msqid_kernel	*msqids;
struct shminfo		shminfo;
struct shmid_kernel	*shmsegs;

struct nlist symbols[] = {
	{ .n_name = "sema" },
	{ .n_name = "seminfo" },
	{ .n_name = "msginfo" },
	{ .n_name = "msqids" },
	{ .n_name = "shminfo" },
	{ .n_name = "shmsegs" },
	{ .n_name = NULL }
};

#define	SHMINFO_XVEC	X(shmmax, sizeof(u_long))			\
			X(shmmin, sizeof(u_long))			\
			X(shmmni, sizeof(u_long))			\
			X(shmseg, sizeof(u_long))			\
			X(shmall, sizeof(u_long))

#define	SEMINFO_XVEC	X(semmni, sizeof(int))				\
			X(semmns, sizeof(int))				\
			X(semmnu, sizeof(int))				\
			X(semmsl, sizeof(int))				\
			X(semopm, sizeof(int))				\
			X(semume, sizeof(int))				\
			X(semusz, sizeof(int))				\
			X(semvmx, sizeof(int))				\
			X(semaem, sizeof(int))

#define	MSGINFO_XVEC	X(msgmax, sizeof(int))				\
			X(msgmni, sizeof(int))				\
			X(msgmnb, sizeof(int))				\
			X(msgtql, sizeof(int))				\
			X(msgssz, sizeof(int))				\
			X(msgseg, sizeof(int))

#define	X(a, b)	{ "kern.ipc." #a, offsetof(TYPEC, a), (b) },
#define	TYPEC	struct shminfo
static struct scgs_vector shminfo_scgsv[] = { SHMINFO_XVEC { .sysctl=NULL } };
#undef	TYPEC
#define	TYPEC	struct seminfo
static struct scgs_vector seminfo_scgsv[] = { SEMINFO_XVEC { .sysctl=NULL } };
#undef	TYPEC
#define	TYPEC	struct msginfo
static struct scgs_vector msginfo_scgsv[] = { MSGINFO_XVEC { .sysctl=NULL } };
#undef	TYPEC
#undef	X

kvm_t *kd;

void
sysctlgatherstruct(void *addr, size_t size, struct scgs_vector *vecarr)
{
	struct scgs_vector *xp;
	size_t tsiz;
	int rv;

	for (xp = vecarr; xp->sysctl != NULL; xp++) {
		assert(xp->offset <= size);
		tsiz = xp->size;
		rv = sysctlbyname(xp->sysctl, (char *)addr + xp->offset,
		    &tsiz, NULL, 0);
		if (rv == -1)
			err(1, "sysctlbyname: %s", xp->sysctl);
		if (tsiz != xp->size)
			errx(1, "%s size mismatch (expected %zu, got %zu)",
			    xp->sysctl, xp->size, tsiz);
	}
}

void
kget(int idx, void *addr, size_t size)
{
	const char *symn;		/* symbol name */
	size_t tsiz;
	int rv;
	unsigned long kaddr;
	const char *sym2sysctl[] = {	/* symbol to sysctl name table */
		"kern.ipc.sema",
		"kern.ipc.seminfo",
		"kern.ipc.msginfo",
		"kern.ipc.msqids",
		"kern.ipc.shminfo",
		"kern.ipc.shmsegs" };

	assert((unsigned)idx <= sizeof(sym2sysctl) / sizeof(*sym2sysctl));
	if (!use_sysctl) {
		symn = symbols[idx].n_name;
		if (*symn == '_')
			symn++;
		if (symbols[idx].n_type == 0 || symbols[idx].n_value == 0)
			errx(1, "symbol %s undefined", symn);
		/*
		 * For some symbols, the value we retrieve is
		 * actually a pointer; since we want the actual value,
		 * we have to manually dereference it.
		 */
		switch (idx) {
		case X_MSQIDS:
			tsiz = sizeof(msqids);
			rv = kvm_read(kd, symbols[idx].n_value,
			    &msqids, tsiz);
			kaddr = (u_long)msqids;
			break;
		case X_SHMSEGS:
			tsiz = sizeof(shmsegs);
			rv = kvm_read(kd, symbols[idx].n_value,
			    &shmsegs, tsiz);
			kaddr = (u_long)shmsegs;
			break;
		case X_SEMA:
			tsiz = sizeof(sema);
			rv = kvm_read(kd, symbols[idx].n_value,
			    &sema, tsiz);
			kaddr = (u_long)sema;
			break;
		default:
			rv = tsiz = 0;
			kaddr = symbols[idx].n_value;
			break;
		}
		if ((unsigned)rv != tsiz)
			errx(1, "%s: %s", symn, kvm_geterr(kd));
		if ((unsigned)kvm_read(kd, kaddr, addr, size) != size)
			errx(1, "%s: %s", symn, kvm_geterr(kd));
	} else {
		switch (idx) {
		case X_SHMINFO:
			sysctlgatherstruct(addr, size, shminfo_scgsv);
			break;
		case X_SEMINFO:
			sysctlgatherstruct(addr, size, seminfo_scgsv);
			break;
		case X_MSGINFO:
			sysctlgatherstruct(addr, size, msginfo_scgsv);
			break;
		default:
			tsiz = size;
			rv = sysctlbyname(sym2sysctl[idx], addr, &tsiz,
			    NULL, 0);
			if (rv == -1)
				err(1, "sysctlbyname: %s", sym2sysctl[idx]);
			if (tsiz != size)
				errx(1, "%s size mismatch "
				    "(expected %zu, got %zu)",
				    sym2sysctl[idx], size, tsiz);
			break;
		}
	}
}
