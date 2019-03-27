/*-
 * Copyright (c) 2015 Nuxi, https://nuxi.nl/
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

#include <sys/param.h>
#include <sys/mman.h>
#include <sys/proc.h>
#include <sys/syscallsubr.h>

#include <contrib/cloudabi/cloudabi_types_common.h>

#include <compat/cloudabi/cloudabi_proto.h>

/* Converts CloudABI's memory protection flags to FreeBSD's. */
static int
convert_mprot(cloudabi_mprot_t in, int *out)
{

	/* Unknown protection flags. */
	if ((in & ~(CLOUDABI_PROT_EXEC | CLOUDABI_PROT_WRITE |
	    CLOUDABI_PROT_READ)) != 0)
		return (ENOTSUP);
	/* W^X: Write and exec cannot be enabled at the same time. */
	if ((in & (CLOUDABI_PROT_EXEC | CLOUDABI_PROT_WRITE)) ==
	    (CLOUDABI_PROT_EXEC | CLOUDABI_PROT_WRITE))
		return (ENOTSUP);

	*out = 0;
	if (in & CLOUDABI_PROT_EXEC)
		*out |= PROT_EXEC;
	if (in & CLOUDABI_PROT_WRITE)
		*out |= PROT_WRITE;
	if (in & CLOUDABI_PROT_READ)
		*out |= PROT_READ;
	return (0);
}

int
cloudabi_sys_mem_advise(struct thread *td,
    struct cloudabi_sys_mem_advise_args *uap)
{
	int behav;

	switch (uap->advice) {
	case CLOUDABI_ADVICE_DONTNEED:
		behav = MADV_DONTNEED;
		break;
	case CLOUDABI_ADVICE_NORMAL:
		behav = MADV_NORMAL;
		break;
	case CLOUDABI_ADVICE_RANDOM:
		behav = MADV_RANDOM;
		break;
	case CLOUDABI_ADVICE_SEQUENTIAL:
		behav = MADV_SEQUENTIAL;
		break;
	case CLOUDABI_ADVICE_WILLNEED:
		behav = MADV_WILLNEED;
		break;
	default:
		return (EINVAL);
	}

	return (kern_madvise(td, (uintptr_t)uap->mapping, uap->mapping_len,
	    behav));
}

int
cloudabi_sys_mem_map(struct thread *td, struct cloudabi_sys_mem_map_args *uap)
{
	int error, flags, prot;

	/* Translate flags. */
	flags = 0;
	if (uap->flags & CLOUDABI_MAP_ANON)
		flags |= MAP_ANON;
	if (uap->flags & CLOUDABI_MAP_FIXED)
		flags |= MAP_FIXED;
	if (uap->flags & CLOUDABI_MAP_PRIVATE)
		flags |= MAP_PRIVATE;
	if (uap->flags & CLOUDABI_MAP_SHARED)
		flags |= MAP_SHARED;

	/* Translate protection. */
	error = convert_mprot(uap->prot, &prot);
	if (error != 0)
		return (error);

	return (kern_mmap(td, (uintptr_t)uap->addr, uap->len, prot, flags,
	    uap->fd, uap->off));
}

int
cloudabi_sys_mem_protect(struct thread *td,
    struct cloudabi_sys_mem_protect_args *uap)
{
	int error, prot;

	/* Translate protection. */
	error = convert_mprot(uap->prot, &prot);
	if (error != 0)
		return (error);

	return (kern_mprotect(td, (uintptr_t)uap->mapping, uap->mapping_len,
	    prot));
}

int
cloudabi_sys_mem_sync(struct thread *td, struct cloudabi_sys_mem_sync_args *uap)
{
	int flags;

	/* Convert flags. */
	switch (uap->flags & (CLOUDABI_MS_ASYNC | CLOUDABI_MS_SYNC)) {
	case CLOUDABI_MS_ASYNC:
		flags = MS_ASYNC;
		break;
	case CLOUDABI_MS_SYNC:
		flags = MS_SYNC;
		break;
	default:
		return (EINVAL);
	}
	if ((uap->flags & CLOUDABI_MS_INVALIDATE) != 0)
		flags |= MS_INVALIDATE;

	return (kern_msync(td, (uintptr_t)uap->mapping, uap->mapping_len,
	    flags));
}

int
cloudabi_sys_mem_unmap(struct thread *td,
    struct cloudabi_sys_mem_unmap_args *uap)
{

	return (kern_munmap(td, (uintptr_t)uap->mapping, uap->mapping_len));
}
