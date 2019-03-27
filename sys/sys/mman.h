/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1986, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)mman.h	8.2 (Berkeley) 1/9/95
 * $FreeBSD$
 */

#ifndef _SYS_MMAN_H_
#define _SYS_MMAN_H_

#include <sys/cdefs.h>
#include <sys/_types.h>

#if __BSD_VISIBLE
/*
 * Inheritance for minherit()
 */
#define INHERIT_SHARE	0
#define INHERIT_COPY	1
#define INHERIT_NONE	2
#define INHERIT_ZERO	3
#endif

/*
 * Protections are chosen from these bits, or-ed together
 */
#define	PROT_NONE	0x00	/* no permissions */
#define	PROT_READ	0x01	/* pages can be read */
#define	PROT_WRITE	0x02	/* pages can be written */
#define	PROT_EXEC	0x04	/* pages can be executed */

/*
 * Flags contain sharing type and options.
 * Sharing types; choose one.
 */
#define	MAP_SHARED	0x0001		/* share changes */
#define	MAP_PRIVATE	0x0002		/* changes are private */
#if __BSD_VISIBLE
#define	MAP_COPY	MAP_PRIVATE	/* Obsolete */
#endif

/*
 * Other flags
 */
#define	MAP_FIXED	 0x0010	/* map addr must be exactly as requested */

#if __BSD_VISIBLE
#define	MAP_RESERVED0020 0x0020	/* previously unimplemented MAP_RENAME */
#define	MAP_RESERVED0040 0x0040	/* previously unimplemented MAP_NORESERVE */
#define	MAP_RESERVED0080 0x0080	/* previously misimplemented MAP_INHERIT */
#define	MAP_RESERVED0100 0x0100	/* previously unimplemented MAP_NOEXTEND */
#define	MAP_HASSEMAPHORE 0x0200	/* region may contain semaphores */
#define	MAP_STACK	 0x0400	/* region grows down, like a stack */
#define	MAP_NOSYNC	 0x0800 /* page to but do not sync underlying file */

/*
 * Mapping type
 */
#define	MAP_FILE	 0x0000	/* map from file (default) */
#define	MAP_ANON	 0x1000	/* allocated from memory, swap space */
#ifndef _KERNEL
#define	MAP_ANONYMOUS	 MAP_ANON /* For compatibility. */
#endif /* !_KERNEL */

/*
 * Extended flags
 */
#define	MAP_GUARD	 0x00002000 /* reserve but don't map address range */
#define	MAP_EXCL	 0x00004000 /* for MAP_FIXED, fail if address is used */
#define	MAP_NOCORE	 0x00020000 /* dont include these pages in a coredump */
#define	MAP_PREFAULT_READ 0x00040000 /* prefault mapping for reading */
#ifdef __LP64__
#define	MAP_32BIT	 0x00080000 /* map in the low 2GB of address space */
#endif

/*
 * Request specific alignment (n == log2 of the desired alignment).
 *
 * MAP_ALIGNED_SUPER requests optimal superpage alignment, but does
 * not enforce a specific alignment.
 */
#define	MAP_ALIGNED(n)	 ((n) << MAP_ALIGNMENT_SHIFT)
#define	MAP_ALIGNMENT_SHIFT	24
#define	MAP_ALIGNMENT_MASK	MAP_ALIGNED(0xff)
#define	MAP_ALIGNED_SUPER	MAP_ALIGNED(1) /* align on a superpage */
#endif /* __BSD_VISIBLE */

#if __POSIX_VISIBLE >= 199309
/*
 * Process memory locking
 */
#define MCL_CURRENT	0x0001	/* Lock only current memory */
#define MCL_FUTURE	0x0002	/* Lock all future memory as well */
#endif

/*
 * Error return from mmap()
 */
#define MAP_FAILED	((void *)-1)

/*
 * msync() flags
 */
#define	MS_SYNC		0x0000	/* msync synchronously */
#define MS_ASYNC	0x0001	/* return immediately */
#define MS_INVALIDATE	0x0002	/* invalidate all cached data */

/*
 * Advice to madvise
 */
#define	_MADV_NORMAL	0	/* no further special treatment */
#define	_MADV_RANDOM	1	/* expect random page references */
#define	_MADV_SEQUENTIAL 2	/* expect sequential page references */
#define	_MADV_WILLNEED	3	/* will need these pages */
#define	_MADV_DONTNEED	4	/* dont need these pages */

#if __BSD_VISIBLE
#define	MADV_NORMAL	_MADV_NORMAL
#define	MADV_RANDOM	_MADV_RANDOM
#define	MADV_SEQUENTIAL _MADV_SEQUENTIAL
#define	MADV_WILLNEED	_MADV_WILLNEED
#define	MADV_DONTNEED	_MADV_DONTNEED
#define	MADV_FREE	5	/* dont need these pages, and junk contents */
#define	MADV_NOSYNC	6	/* try to avoid flushes to physical media */
#define	MADV_AUTOSYNC	7	/* revert to default flushing strategy */
#define	MADV_NOCORE	8	/* do not include these pages in a core file */
#define	MADV_CORE	9	/* revert to including pages in a core file */
#define	MADV_PROTECT	10	/* protect process from pageout kill */

/*
 * Return bits from mincore
 */
#define	MINCORE_INCORE	 	 0x1 /* Page is incore */
#define	MINCORE_REFERENCED	 0x2 /* Page has been referenced by us */
#define	MINCORE_MODIFIED	 0x4 /* Page has been modified by us */
#define	MINCORE_REFERENCED_OTHER 0x8 /* Page has been referenced */
#define	MINCORE_MODIFIED_OTHER	0x10 /* Page has been modified */
#define	MINCORE_SUPER		0x20 /* Page is a "super" page */

/*
 * Anonymous object constant for shm_open().
 */
#define	SHM_ANON		((char *)1)
#endif /* __BSD_VISIBLE */

/*
 * XXX missing POSIX_TYPED_MEM_* macros and
 * posix_typed_mem_info structure.
 */
#if __POSIX_VISIBLE >= 200112
#define	POSIX_MADV_NORMAL	_MADV_NORMAL
#define	POSIX_MADV_RANDOM	_MADV_RANDOM
#define	POSIX_MADV_SEQUENTIAL	_MADV_SEQUENTIAL
#define	POSIX_MADV_WILLNEED	_MADV_WILLNEED
#define	POSIX_MADV_DONTNEED	_MADV_DONTNEED
#endif

#ifndef _MODE_T_DECLARED
typedef	__mode_t	mode_t;
#define	_MODE_T_DECLARED
#endif

#ifndef _OFF_T_DECLARED
typedef	__off_t		off_t;
#define	_OFF_T_DECLARED
#endif

#ifndef _SIZE_T_DECLARED
typedef	__size_t	size_t;
#define	_SIZE_T_DECLARED
#endif

#if defined(_KERNEL) || defined(_WANT_FILE)
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/queue.h>
#include <sys/rangelock.h>
#include <vm/vm.h>

struct file;

struct shmfd {
	size_t		shm_size;
	vm_object_t	shm_object;
	int		shm_refs;
	uid_t		shm_uid;
	gid_t		shm_gid;
	mode_t		shm_mode;
	int		shm_kmappings;

	/*
	 * Values maintained solely to make this a better-behaved file
	 * descriptor for fstat() to run on.
	 */
	struct timespec	shm_atime;
	struct timespec	shm_mtime;
	struct timespec	shm_ctime;
	struct timespec	shm_birthtime;
	ino_t		shm_ino;

	struct label	*shm_label;		/* MAC label */
	const char	*shm_path;

	struct rangelock shm_rl;
	struct mtx	shm_mtx;
};
#endif

#ifdef _KERNEL
int	shm_map(struct file *fp, size_t size, off_t offset, void **memp);
int	shm_unmap(struct file *fp, void *mem, size_t size);

int	shm_access(struct shmfd *shmfd, struct ucred *ucred, int flags);
struct shmfd *shm_alloc(struct ucred *ucred, mode_t mode);
struct shmfd *shm_hold(struct shmfd *shmfd);
void	shm_drop(struct shmfd *shmfd);
int	shm_dotruncate(struct shmfd *shmfd, off_t length);

extern struct fileops shm_ops;
#else /* !_KERNEL */

__BEGIN_DECLS
/*
 * XXX not yet implemented: posix_mem_offset(), posix_typed_mem_get_info(),
 * posix_typed_mem_open().
 */
#if __BSD_VISIBLE
int	getpagesizes(size_t *, int);
int	madvise(void *, size_t, int);
int	mincore(const void *, size_t, char *);
int	minherit(void *, size_t, int);
#endif
int	mlock(const void *, size_t);
#ifndef _MMAP_DECLARED
#define	_MMAP_DECLARED
void *	mmap(void *, size_t, int, int, int, off_t);
#endif
int	mprotect(void *, size_t, int);
int	msync(void *, size_t, int);
int	munlock(const void *, size_t);
int	munmap(void *, size_t);
#if __POSIX_VISIBLE >= 200112
int	posix_madvise(void *, size_t, int);
#endif
#if __POSIX_VISIBLE >= 199309
int	mlockall(int);
int	munlockall(void);
int	shm_open(const char *, int, mode_t);
int	shm_unlink(const char *);
#endif
__END_DECLS

#endif /* !_KERNEL */

#endif /* !_SYS_MMAN_H_ */
