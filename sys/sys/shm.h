/* $FreeBSD$ */
/*	$NetBSD: shm.h,v 1.15 1994/06/29 06:45:17 cgd Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1994 Adam Glass
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Adam Glass.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * As defined+described in "X/Open System Interfaces and Headers"
 *                         Issue 4, p. XXX
 */

#ifndef _SYS_SHM_H_
#define _SYS_SHM_H_

#include <sys/cdefs.h>
#ifdef _WANT_SYSVSHM_INTERNALS
#define	_WANT_SYSVIPC_INTERNALS
#endif
#include <sys/ipc.h>
#include <sys/_types.h>

#include <machine/param.h>

#define SHM_RDONLY  010000  /* Attach read-only (else read-write) */
#define SHM_RND     020000  /* Round attach address to SHMLBA */
#define	SHM_REMAP   030000  /* Unmap before mapping */
#define SHMLBA      PAGE_SIZE /* Segment low boundary address multiple */

/* "official" access mode definitions; somewhat braindead since you have
   to specify (SHM_* >> 3) for group and (SHM_* >> 6) for world permissions */
#define SHM_R       (IPC_R)
#define SHM_W       (IPC_W)

/* predefine tbd *LOCK shmctl commands */
#define	SHM_LOCK	11
#define	SHM_UNLOCK	12

/* ipcs shmctl commands for Linux compatibility */
#define	SHM_STAT	13
#define	SHM_INFO	14

#ifndef _PID_T_DECLARED
typedef	__pid_t		pid_t;
#define	_PID_T_DECLARED
#endif

#ifndef _TIME_T_DECLARED
typedef	__time_t	time_t;
#define	_TIME_T_DECLARED
#endif

#ifndef _SIZE_T_DECLARED
typedef	__size_t	size_t;
#define	_SIZE_T_DECLARED
#endif

#if defined(COMPAT_FREEBSD4) || defined(COMPAT_FREEBSD5) || \
    defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD7)
struct shmid_ds_old {
	struct ipc_perm_old shm_perm;	/* operation permission structure */
	int             shm_segsz;	/* size of segment in bytes */
	pid_t           shm_lpid;   /* process ID of last shared memory op */
	pid_t           shm_cpid;	/* process ID of creator */
	short		shm_nattch;	/* number of current attaches */
	time_t          shm_atime;	/* time of last shmat() */
	time_t          shm_dtime;	/* time of last shmdt() */
	time_t          shm_ctime;	/* time of last change by shmctl() */
	void           *shm_internal;   /* sysv stupidity */
};
#endif

typedef unsigned int shmatt_t;

struct shmid_ds {
	struct ipc_perm shm_perm;	/* operation permission structure */
	size_t          shm_segsz;	/* size of segment in bytes */
	pid_t           shm_lpid;   /* process ID of last shared memory op */
	pid_t           shm_cpid;	/* process ID of creator */
	shmatt_t        shm_nattch;	/* number of current attaches */
	time_t          shm_atime;	/* time of last shmat() */
	time_t          shm_dtime;	/* time of last shmdt() */
	time_t          shm_ctime;	/* time of last change by shmctl() */
};

#if defined(_KERNEL) || defined(_WANT_SYSVSHM_INTERNALS)
/*
 * System 5 style catch-all structure for shared memory constants that
 * might be of interest to user programs.  Do we really want/need this?
 */
struct shminfo {
	u_long	shmmax;		/* max shared memory segment size (bytes) */
	u_long	shmmin;		/* max shared memory segment size (bytes) */
	u_long	shmmni;		/* max number of shared memory identifiers */
	u_long	shmseg;		/* max shared memory segments per process */
	u_long	shmall;		/* max amount of shared memory (pages) */
};

struct vm_object;

/* 
 * Add a kernel wrapper to the shmid_ds struct so that private info (like the
 * MAC label) can be added to it, without changing the user interface.
 */
struct shmid_kernel {
	struct shmid_ds u;
	struct vm_object *object;
	struct label *label;	/* MAC label */
	struct ucred *cred;	/* creator's credendials */
};
#endif

struct shm_info {
	int used_ids;
	unsigned long shm_tot;
	unsigned long shm_rss;
	unsigned long shm_swp;
	unsigned long swap_attempts;
	unsigned long swap_successes;
};

#ifdef _KERNEL
struct proc;
struct vmspace;

extern struct shminfo	shminfo;

void	shmexit(struct vmspace *);
void	shmfork(struct proc *, struct proc *);

#else /* !_KERNEL */

#include <sys/cdefs.h>

#ifndef _SIZE_T_DECLARED
typedef __size_t        size_t;
#define _SIZE_T_DECLARED
#endif

__BEGIN_DECLS
#if __BSD_VISIBLE
int shmsys(int, ...);
#endif
void *shmat(int, const void *, int);
int shmget(key_t, size_t, int);
int shmctl(int, int, struct shmid_ds *);
int shmdt(const void *);
__END_DECLS

#endif /* _KERNEL */

#endif /* !_SYS_SHM_H_ */
