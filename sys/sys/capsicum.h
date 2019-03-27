/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008-2010, 2015 Robert N. M. Watson
 * Copyright (c) 2012 FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed at the University of Cambridge Computer
 * Laboratory with support from a grant from Google, Inc.
 *
 * Portions of this software were developed by Pawel Jakub Dawidek under
 * sponsorship from the FreeBSD Foundation.
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

/*
 * Definitions for FreeBSD capabilities facility.
 */
#ifndef _SYS_CAPSICUM_H_
#define	_SYS_CAPSICUM_H_

#include <sys/cdefs.h>
#include <sys/param.h>

#include <sys/caprights.h>
#include <sys/file.h>
#include <sys/fcntl.h>

#ifndef _KERNEL
#include <stdbool.h>
#endif

#define	CAPRIGHT(idx, bit)	((1ULL << (57 + (idx))) | (bit))

/*
 * Possible rights on capabilities.
 *
 * Notes:
 * Some system calls don't require a capability in order to perform an
 * operation on an fd.  These include: close, dup, dup2.
 *
 * sendfile is authorized using CAP_READ on the file and CAP_WRITE on the
 * socket.
 *
 * mmap() and aio*() system calls will need special attention as they may
 * involve reads or writes depending a great deal on context.
 */

/* INDEX 0 */

/*
 * General file I/O.
 */
/* Allows for openat(O_RDONLY), read(2), readv(2). */
#define	CAP_READ		CAPRIGHT(0, 0x0000000000000001ULL)
/* Allows for openat(O_WRONLY | O_APPEND), write(2), writev(2). */
#define	CAP_WRITE		CAPRIGHT(0, 0x0000000000000002ULL)
/* Allows for lseek(fd, 0, SEEK_CUR). */
#define	CAP_SEEK_TELL		CAPRIGHT(0, 0x0000000000000004ULL)
/* Allows for lseek(2). */
#define	CAP_SEEK		(CAP_SEEK_TELL | 0x0000000000000008ULL)
/* Allows for aio_read(2), pread(2), preadv(2). */
#define	CAP_PREAD		(CAP_SEEK | CAP_READ)
/*
 * Allows for aio_write(2), openat(O_WRONLY) (without O_APPEND), pwrite(2),
 * pwritev(2).
 */
#define	CAP_PWRITE		(CAP_SEEK | CAP_WRITE)
/* Allows for mmap(PROT_NONE). */
#define	CAP_MMAP		CAPRIGHT(0, 0x0000000000000010ULL)
/* Allows for mmap(PROT_READ). */
#define	CAP_MMAP_R		(CAP_MMAP | CAP_SEEK | CAP_READ)
/* Allows for mmap(PROT_WRITE). */
#define	CAP_MMAP_W		(CAP_MMAP | CAP_SEEK | CAP_WRITE)
/* Allows for mmap(PROT_EXEC). */
#define	CAP_MMAP_X		(CAP_MMAP | CAP_SEEK | 0x0000000000000020ULL)
/* Allows for mmap(PROT_READ | PROT_WRITE). */
#define	CAP_MMAP_RW		(CAP_MMAP_R | CAP_MMAP_W)
/* Allows for mmap(PROT_READ | PROT_EXEC). */
#define	CAP_MMAP_RX		(CAP_MMAP_R | CAP_MMAP_X)
/* Allows for mmap(PROT_WRITE | PROT_EXEC). */
#define	CAP_MMAP_WX		(CAP_MMAP_W | CAP_MMAP_X)
/* Allows for mmap(PROT_READ | PROT_WRITE | PROT_EXEC). */
#define	CAP_MMAP_RWX		(CAP_MMAP_R | CAP_MMAP_W | CAP_MMAP_X)
/* Allows for openat(O_CREAT). */
#define	CAP_CREATE		CAPRIGHT(0, 0x0000000000000040ULL)
/* Allows for openat(O_EXEC) and fexecve(2) in turn. */
#define	CAP_FEXECVE		CAPRIGHT(0, 0x0000000000000080ULL)
/* Allows for openat(O_SYNC), openat(O_FSYNC), fsync(2), aio_fsync(2). */
#define	CAP_FSYNC		CAPRIGHT(0, 0x0000000000000100ULL)
/* Allows for openat(O_TRUNC), ftruncate(2). */
#define	CAP_FTRUNCATE		CAPRIGHT(0, 0x0000000000000200ULL)

/* Lookups - used to constrain *at() calls. */
#define	CAP_LOOKUP		CAPRIGHT(0, 0x0000000000000400ULL)

/* VFS methods. */
/* Allows for fchdir(2). */
#define	CAP_FCHDIR		CAPRIGHT(0, 0x0000000000000800ULL)
/* Allows for fchflags(2). */
#define	CAP_FCHFLAGS		CAPRIGHT(0, 0x0000000000001000ULL)
/* Allows for fchflags(2) and chflagsat(2). */
#define	CAP_CHFLAGSAT		(CAP_FCHFLAGS | CAP_LOOKUP)
/* Allows for fchmod(2). */
#define	CAP_FCHMOD		CAPRIGHT(0, 0x0000000000002000ULL)
/* Allows for fchmod(2) and fchmodat(2). */
#define	CAP_FCHMODAT		(CAP_FCHMOD | CAP_LOOKUP)
/* Allows for fchown(2). */
#define	CAP_FCHOWN		CAPRIGHT(0, 0x0000000000004000ULL)
/* Allows for fchown(2) and fchownat(2). */
#define	CAP_FCHOWNAT		(CAP_FCHOWN | CAP_LOOKUP)
/* Allows for fcntl(2). */
#define	CAP_FCNTL		CAPRIGHT(0, 0x0000000000008000ULL)
/*
 * Allows for flock(2), openat(O_SHLOCK), openat(O_EXLOCK),
 * fcntl(F_SETLK_REMOTE), fcntl(F_SETLKW), fcntl(F_SETLK), fcntl(F_GETLK).
 */
#define	CAP_FLOCK		CAPRIGHT(0, 0x0000000000010000ULL)
/* Allows for fpathconf(2). */
#define	CAP_FPATHCONF		CAPRIGHT(0, 0x0000000000020000ULL)
/* Allows for UFS background-fsck operations. */
#define	CAP_FSCK		CAPRIGHT(0, 0x0000000000040000ULL)
/* Allows for fstat(2). */
#define	CAP_FSTAT		CAPRIGHT(0, 0x0000000000080000ULL)
/* Allows for fstat(2), fstatat(2) and faccessat(2). */
#define	CAP_FSTATAT		(CAP_FSTAT | CAP_LOOKUP)
/* Allows for fstatfs(2). */
#define	CAP_FSTATFS		CAPRIGHT(0, 0x0000000000100000ULL)
/* Allows for futimens(2) and futimes(2). */
#define	CAP_FUTIMES		CAPRIGHT(0, 0x0000000000200000ULL)
/* Allows for futimens(2), futimes(2), futimesat(2) and utimensat(2). */
#define	CAP_FUTIMESAT		(CAP_FUTIMES | CAP_LOOKUP)
/* Allows for linkat(2) (target directory descriptor). */
#define	CAP_LINKAT_TARGET	(CAP_LOOKUP | 0x0000000000400000ULL)
/* Allows for mkdirat(2). */
#define	CAP_MKDIRAT		(CAP_LOOKUP | 0x0000000000800000ULL)
/* Allows for mkfifoat(2). */
#define	CAP_MKFIFOAT		(CAP_LOOKUP | 0x0000000001000000ULL)
/* Allows for mknodat(2). */
#define	CAP_MKNODAT		(CAP_LOOKUP | 0x0000000002000000ULL)
/* Allows for renameat(2) (source directory descriptor). */
#define	CAP_RENAMEAT_SOURCE	(CAP_LOOKUP | 0x0000000004000000ULL)
/* Allows for symlinkat(2). */
#define	CAP_SYMLINKAT		(CAP_LOOKUP | 0x0000000008000000ULL)
/*
 * Allows for unlinkat(2) and renameat(2) if destination object exists and
 * will be removed.
 */
#define	CAP_UNLINKAT		(CAP_LOOKUP | 0x0000000010000000ULL)

/* Socket operations. */
/* Allows for accept(2) and accept4(2). */
#define	CAP_ACCEPT		CAPRIGHT(0, 0x0000000020000000ULL)
/* Allows for bind(2). */
#define	CAP_BIND		CAPRIGHT(0, 0x0000000040000000ULL)
/* Allows for connect(2). */
#define	CAP_CONNECT		CAPRIGHT(0, 0x0000000080000000ULL)
/* Allows for getpeername(2). */
#define	CAP_GETPEERNAME		CAPRIGHT(0, 0x0000000100000000ULL)
/* Allows for getsockname(2). */
#define	CAP_GETSOCKNAME		CAPRIGHT(0, 0x0000000200000000ULL)
/* Allows for getsockopt(2). */
#define	CAP_GETSOCKOPT		CAPRIGHT(0, 0x0000000400000000ULL)
/* Allows for listen(2). */
#define	CAP_LISTEN		CAPRIGHT(0, 0x0000000800000000ULL)
/* Allows for sctp_peeloff(2). */
#define	CAP_PEELOFF		CAPRIGHT(0, 0x0000001000000000ULL)
#define	CAP_RECV		CAP_READ
#define	CAP_SEND		CAP_WRITE
/* Allows for setsockopt(2). */
#define	CAP_SETSOCKOPT		CAPRIGHT(0, 0x0000002000000000ULL)
/* Allows for shutdown(2). */
#define	CAP_SHUTDOWN		CAPRIGHT(0, 0x0000004000000000ULL)

/* Allows for bindat(2) on a directory descriptor. */
#define	CAP_BINDAT		(CAP_LOOKUP | 0x0000008000000000ULL)
/* Allows for connectat(2) on a directory descriptor. */
#define	CAP_CONNECTAT		(CAP_LOOKUP | 0x0000010000000000ULL)

/* Allows for linkat(2) (source directory descriptor). */
#define	CAP_LINKAT_SOURCE	(CAP_LOOKUP | 0x0000020000000000ULL)
/* Allows for renameat(2) (target directory descriptor). */
#define	CAP_RENAMEAT_TARGET	(CAP_LOOKUP | 0x0000040000000000ULL)

#define	CAP_SOCK_CLIENT \
	(CAP_CONNECT | CAP_GETPEERNAME | CAP_GETSOCKNAME | CAP_GETSOCKOPT | \
	 CAP_PEELOFF | CAP_RECV | CAP_SEND | CAP_SETSOCKOPT | CAP_SHUTDOWN)
#define	CAP_SOCK_SERVER \
	(CAP_ACCEPT | CAP_BIND | CAP_GETPEERNAME | CAP_GETSOCKNAME | \
	 CAP_GETSOCKOPT | CAP_LISTEN | CAP_PEELOFF | CAP_RECV | CAP_SEND | \
	 CAP_SETSOCKOPT | CAP_SHUTDOWN)

/* All used bits for index 0. */
#define	CAP_ALL0		CAPRIGHT(0, 0x000007FFFFFFFFFFULL)

/* Available bits for index 0. */
#define	CAP_UNUSED0_44		CAPRIGHT(0, 0x0000080000000000ULL)
/* ... */
#define	CAP_UNUSED0_57		CAPRIGHT(0, 0x0100000000000000ULL)

/* INDEX 1 */

/* Mandatory Access Control. */
/* Allows for mac_get_fd(3). */
#define	CAP_MAC_GET		CAPRIGHT(1, 0x0000000000000001ULL)
/* Allows for mac_set_fd(3). */
#define	CAP_MAC_SET		CAPRIGHT(1, 0x0000000000000002ULL)

/* Methods on semaphores. */
#define	CAP_SEM_GETVALUE	CAPRIGHT(1, 0x0000000000000004ULL)
#define	CAP_SEM_POST		CAPRIGHT(1, 0x0000000000000008ULL)
#define	CAP_SEM_WAIT		CAPRIGHT(1, 0x0000000000000010ULL)

/* Allows select(2) and poll(2) on descriptor. */
#define	CAP_EVENT		CAPRIGHT(1, 0x0000000000000020ULL)
/* Allows for kevent(2) on kqueue descriptor with eventlist != NULL. */
#define	CAP_KQUEUE_EVENT	CAPRIGHT(1, 0x0000000000000040ULL)

/* Strange and powerful rights that should not be given lightly. */
/* Allows for ioctl(2). */
#define	CAP_IOCTL		CAPRIGHT(1, 0x0000000000000080ULL)
#define	CAP_TTYHOOK		CAPRIGHT(1, 0x0000000000000100ULL)

/* Process management via process descriptors. */
/* Allows for pdgetpid(2). */
#define	CAP_PDGETPID		CAPRIGHT(1, 0x0000000000000200ULL)
/*
 * Allows for pdwait4(2).
 *
 * XXX: this constant was imported unused, but is targeted to be implemented
 *      in the future (bug 235871).
 */
#define	CAP_PDWAIT		CAPRIGHT(1, 0x0000000000000400ULL)
/* Allows for pdkill(2). */
#define	CAP_PDKILL		CAPRIGHT(1, 0x0000000000000800ULL)

/* Extended attributes. */
/* Allows for extattr_delete_fd(2). */
#define	CAP_EXTATTR_DELETE	CAPRIGHT(1, 0x0000000000001000ULL)
/* Allows for extattr_get_fd(2). */
#define	CAP_EXTATTR_GET		CAPRIGHT(1, 0x0000000000002000ULL)
/* Allows for extattr_list_fd(2). */
#define	CAP_EXTATTR_LIST	CAPRIGHT(1, 0x0000000000004000ULL)
/* Allows for extattr_set_fd(2). */
#define	CAP_EXTATTR_SET		CAPRIGHT(1, 0x0000000000008000ULL)

/* Access Control Lists. */
/* Allows for acl_valid_fd_np(3). */
#define	CAP_ACL_CHECK		CAPRIGHT(1, 0x0000000000010000ULL)
/* Allows for acl_delete_fd_np(3). */
#define	CAP_ACL_DELETE		CAPRIGHT(1, 0x0000000000020000ULL)
/* Allows for acl_get_fd(3) and acl_get_fd_np(3). */
#define	CAP_ACL_GET		CAPRIGHT(1, 0x0000000000040000ULL)
/* Allows for acl_set_fd(3) and acl_set_fd_np(3). */
#define	CAP_ACL_SET		CAPRIGHT(1, 0x0000000000080000ULL)

/* Allows for kevent(2) on kqueue descriptor with changelist != NULL. */
#define	CAP_KQUEUE_CHANGE	CAPRIGHT(1, 0x0000000000100000ULL)

#define	CAP_KQUEUE		(CAP_KQUEUE_EVENT | CAP_KQUEUE_CHANGE)

/* All used bits for index 1. */
#define	CAP_ALL1		CAPRIGHT(1, 0x00000000001FFFFFULL)

/* Available bits for index 1. */
#define	CAP_UNUSED1_22		CAPRIGHT(1, 0x0000000000200000ULL)
/* ... */
#define	CAP_UNUSED1_57		CAPRIGHT(1, 0x0100000000000000ULL)

/* Backward compatibility. */
#define	CAP_POLL_EVENT		CAP_EVENT

#define	CAP_ALL(rights)		do {					\
	(rights)->cr_rights[0] =					\
	    ((uint64_t)CAP_RIGHTS_VERSION << 62) | CAP_ALL0;		\
	(rights)->cr_rights[1] = CAP_ALL1;				\
} while (0)

#define	CAP_NONE(rights)	do {					\
	(rights)->cr_rights[0] =					\
	    ((uint64_t)CAP_RIGHTS_VERSION << 62) | CAPRIGHT(0, 0ULL);	\
	(rights)->cr_rights[1] = CAPRIGHT(1, 0ULL);			\
} while (0)

#define	CAPRVER(right)		((int)((right) >> 62))
#define	CAPVER(rights)		CAPRVER((rights)->cr_rights[0])
#define	CAPARSIZE(rights)	(CAPVER(rights) + 2)
#define	CAPIDXBIT(right)	((int)(((right) >> 57) & 0x1F))

/*
 * Allowed fcntl(2) commands.
 */
#define	CAP_FCNTL_GETFL		(1 << F_GETFL)
#define	CAP_FCNTL_SETFL		(1 << F_SETFL)
#define	CAP_FCNTL_GETOWN	(1 << F_GETOWN)
#define	CAP_FCNTL_SETOWN	(1 << F_SETOWN)
#define	CAP_FCNTL_ALL		(CAP_FCNTL_GETFL | CAP_FCNTL_SETFL | \
				 CAP_FCNTL_GETOWN | CAP_FCNTL_SETOWN)

#define	CAP_IOCTLS_ALL	SSIZE_MAX

__BEGIN_DECLS

#define	cap_rights_init(...)						\
	__cap_rights_init(CAP_RIGHTS_VERSION, __VA_ARGS__, 0ULL)
cap_rights_t *__cap_rights_init(int version, cap_rights_t *rights, ...);

#define	cap_rights_set(...)						\
	__cap_rights_set(__VA_ARGS__, 0ULL)
cap_rights_t *__cap_rights_set(cap_rights_t *rights, ...);

#define	cap_rights_clear(...)						\
	__cap_rights_clear(__VA_ARGS__, 0ULL)
cap_rights_t *__cap_rights_clear(cap_rights_t *rights, ...);

#define	cap_rights_is_set(...)						\
	__cap_rights_is_set(__VA_ARGS__, 0ULL)
bool __cap_rights_is_set(const cap_rights_t *rights, ...);

bool cap_rights_is_valid(const cap_rights_t *rights);
cap_rights_t *cap_rights_merge(cap_rights_t *dst, const cap_rights_t *src);
cap_rights_t *cap_rights_remove(cap_rights_t *dst, const cap_rights_t *src);
bool cap_rights_contains(const cap_rights_t *big, const cap_rights_t *little);
void __cap_rights_sysinit(void *arg);

__END_DECLS
struct cap_rights_init_args {
	cap_rights_t *cria_rights;
	uint64_t cria_value1;
	uint64_t cria_value2;
	uint64_t cria_value3;
	uint64_t cria_value4;
	uint64_t cria_value5;
};

#define CAP_RIGHTS_SYSINIT0(name, rights)		   \
		static struct cap_rights_init_args name##_args = { \
			&(rights)										\
		};																\
		SYSINIT(name##_cap_rights_sysinit, SI_SUB_COPYRIGHT+1, SI_ORDER_ANY, \
		    __cap_rights_sysinit, &name##_args);

#define CAP_RIGHTS_SYSINIT1(name, rights, value1)		   \
		static struct cap_rights_init_args name##_args = { \
			&(rights),										\
			(value1)										\
		};																\
		SYSINIT(name##_cap_rights_sysinit, SI_SUB_COPYRIGHT+1, SI_ORDER_ANY, \
		    __cap_rights_sysinit, &name##_args);

#define CAP_RIGHTS_SYSINIT2(name, rights, value1, value2)		   \
		static struct cap_rights_init_args name##_args = { \
			&(rights),										\
			(value1),										\
			(value2)													\
		};																\
		SYSINIT(name##_cap_rights_sysinit, SI_SUB_COPYRIGHT, SI_ORDER_ANY, \
		    __cap_rights_sysinit, &name##_args);

#define CAP_RIGHTS_SYSINIT3(name, rights, value1, value2, value3) \
		static struct cap_rights_init_args name##_args = { \
			&(rights),										\
			(value1),										\
			(value2),										\
			(value3)													\
		};																\
		SYSINIT(name##_cap_rights_sysinit, SI_SUB_COPYRIGHT, SI_ORDER_ANY, \
		    __cap_rights_sysinit, &name##_args);

#define CAP_RIGHTS_SYSINIT4(name, rights, value1, value2, value3, value4)	\
		static struct cap_rights_init_args name##_args = { \
			&(rights),										\
			(value1),										\
			(value2),										\
			(value3),										\
			(value4)													\
		};																\
		SYSINIT(name##_cap_rights_sysinit, SI_SUB_COPYRIGHT, SI_ORDER_ANY, \
		    __cap_rights_sysinit, &name##_args);

#define CAP_RIGHTS_DEFINE1(name, value)								\
	__read_mostly cap_rights_t name;					\
	CAP_RIGHTS_SYSINIT1(name, name, value);

#ifdef _KERNEL

#include <sys/systm.h>
extern cap_rights_t cap_accept_rights;
extern cap_rights_t cap_bind_rights;
extern cap_rights_t cap_connect_rights;
extern cap_rights_t cap_event_rights;
extern cap_rights_t cap_fchdir_rights;
extern cap_rights_t cap_fchflags_rights;
extern cap_rights_t cap_fchmod_rights;
extern cap_rights_t cap_fchown_rights;
extern cap_rights_t cap_fcntl_rights;
extern cap_rights_t cap_fexecve_rights;
extern cap_rights_t cap_flock_rights;
extern cap_rights_t cap_fpathconf_rights;
extern cap_rights_t cap_fstat_rights;
extern cap_rights_t cap_fstatfs_rights;
extern cap_rights_t cap_fsync_rights;
extern cap_rights_t cap_ftruncate_rights;
extern cap_rights_t cap_futimes_rights;
extern cap_rights_t cap_getpeername_rights;
extern cap_rights_t cap_getsockopt_rights;
extern cap_rights_t cap_getsockname_rights;
extern cap_rights_t cap_ioctl_rights;
extern cap_rights_t cap_linkat_source_rights;
extern cap_rights_t cap_linkat_target_rights;
extern cap_rights_t cap_listen_rights;
extern cap_rights_t cap_mkdirat_rights;
extern cap_rights_t cap_mkfifoat_rights;
extern cap_rights_t cap_mknodat_rights;
extern cap_rights_t cap_mmap_rights;
extern cap_rights_t cap_no_rights;
extern cap_rights_t cap_pdgetpid_rights;
extern cap_rights_t cap_pdkill_rights;
extern cap_rights_t cap_pread_rights;
extern cap_rights_t cap_pwrite_rights;
extern cap_rights_t cap_read_rights;
extern cap_rights_t cap_recv_rights;
extern cap_rights_t cap_renameat_source_rights;
extern cap_rights_t cap_renameat_target_rights;
extern cap_rights_t cap_seek_rights;
extern cap_rights_t cap_send_rights;
extern cap_rights_t cap_send_connect_rights;
extern cap_rights_t cap_setsockopt_rights;
extern cap_rights_t cap_shutdown_rights;
extern cap_rights_t cap_symlinkat_rights;
extern cap_rights_t cap_unlinkat_rights;
extern cap_rights_t cap_write_rights;

#define IN_CAPABILITY_MODE(td) (((td)->td_ucred->cr_flags & CRED_FLAG_CAPMODE) != 0)

struct filedesc;
struct filedescent;

/*
 * Test whether a capability grants the requested rights.
 */
int	cap_check(const cap_rights_t *havep, const cap_rights_t *needp);
/*
 * Convert capability rights into VM access flags.
 */
u_char	cap_rights_to_vmprot(const cap_rights_t *havep);

/*
 * For the purposes of procstat(1) and similar tools, allow kern_descrip.c to
 * extract the rights from a capability.
 *
 * Dereferencing fdep requires filedesc.h, but including it would cause
 * significant pollution. Instead add a macro for consumers which want it,
 * most notably kern_descrip.c.
 */
#define cap_rights_fde_inline(fdep)	(&(fdep)->fde_rights)

const cap_rights_t	*cap_rights_fde(const struct filedescent *fde);
const cap_rights_t	*cap_rights(struct filedesc *fdp, int fd);

int	cap_ioctl_check(struct filedesc *fdp, int fd, u_long cmd);
int	cap_fcntl_check_fde(struct filedescent *fde, int cmd);
int	cap_fcntl_check(struct filedesc *fdp, int fd, int cmd);

extern bool trap_enotcap;

#else /* !_KERNEL */

__BEGIN_DECLS
/*
 * cap_enter(): Cause the process to enter capability mode, which will
 * prevent it from directly accessing global namespaces.  System calls will
 * be limited to process-local, process-inherited, or file descriptor
 * operations.  If already in capability mode, a no-op.
 */
int	cap_enter(void);

/*
 * Are we sandboxed (in capability mode)?
 * This is a libc wrapper around the cap_getmode(2) system call.
 */
bool	cap_sandboxed(void);

/*
 * cap_getmode(): Are we in capability mode?
 */
int	cap_getmode(u_int *modep);

/*
 * Limits capability rights for the given descriptor (CAP_*).
 */
int cap_rights_limit(int fd, const cap_rights_t *rights);
/*
 * Returns capability rights for the given descriptor.
 */
#define	cap_rights_get(fd, rights)					\
	__cap_rights_get(CAP_RIGHTS_VERSION, (fd), (rights))
int __cap_rights_get(int version, int fd, cap_rights_t *rights);
/*
 * Limits allowed ioctls for the given descriptor.
 */
int cap_ioctls_limit(int fd, const cap_ioctl_t *cmds, size_t ncmds);
/*
 * Returns array of allowed ioctls for the given descriptor.
 * If all ioctls are allowed, the cmds array is not populated and
 * the function returns CAP_IOCTLS_ALL.
 */
ssize_t cap_ioctls_get(int fd, cap_ioctl_t *cmds, size_t maxcmds);
/*
 * Limits allowed fcntls for the given descriptor (CAP_FCNTL_*).
 */
int cap_fcntls_limit(int fd, uint32_t fcntlrights);
/*
 * Returns bitmask of allowed fcntls for the given descriptor.
 */
int cap_fcntls_get(int fd, uint32_t *fcntlrightsp);

__END_DECLS

#endif /* !_KERNEL */

#endif /* !_SYS_CAPSICUM_H_ */
