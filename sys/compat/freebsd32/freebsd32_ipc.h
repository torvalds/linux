/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 Doug Rabson
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

#ifndef _COMPAT_FREEBSD32_FREEBSD32_IPC_H_
#define _COMPAT_FREEBSD32_FREEBSD32_IPC_H_

struct ipc_perm32 {
	uid_t		cuid;
	gid_t		cgid;
	uid_t		uid;
	gid_t		gid;
	mode_t		mode;
	uint16_t	seq;
	uint32_t	key;
};

struct semid_ds32 {
	struct ipc_perm32 sem_perm;
	uint32_t	__sem_base;
	unsigned short	sem_nsems;
	int32_t		sem_otime;
	int32_t		sem_ctime;
};

#ifdef _KERNEL
struct semid_kernel32 {
	/* Data structure exposed to user space. */
	struct semid_ds32	u;

	/* Kernel-private components of the semaphore. */
	int32_t			label;
	int32_t			cred;
};
#endif /* _KERNEL */


union semun32 {
	int		val;
	uint32_t	buf;
	uint32_t	array;
};

struct msqid_ds32 {
	struct ipc_perm32 msg_perm;
	uint32_t	__msg_first;
	uint32_t	__msg_last;
	uint32_t	msg_cbytes;
	uint32_t	msg_qnum;
	uint32_t	msg_qbytes;
	pid_t		msg_lspid;
	pid_t		msg_lrpid;
	int32_t		msg_stime;
	int32_t		msg_rtime;
	int32_t		msg_ctime;
};

#ifdef _KERNEL
struct msqid_kernel32 {
	/* Data structure exposed to user space. */
	struct msqid_ds32	u;

	/* Kernel-private components of the message queue. */
	uint32_t		label;
	uint32_t		cred;
};
#endif

struct shmid_ds32 {
	struct ipc_perm32 shm_perm;
	int32_t		shm_segsz;
	pid_t		shm_lpid;
	pid_t		shm_cpid;
	unsigned int	shm_nattch;
	int32_t		shm_atime;
	int32_t		shm_dtime;
	int32_t		shm_ctime;
};

#ifdef _KERNEL
struct shmid_kernel32 {
	struct shmid_ds32	 u;
	int32_t			*object;
	int32_t			*label;
	int32_t			*cred;
};
#endif

struct shm_info32 {
	int32_t		used_ids;
	uint32_t	shm_tot;
	uint32_t	shm_rss;
	uint32_t	shm_swp;
	uint32_t	swap_attempts;
	uint32_t	swap_successes;
};

struct shminfo32 {
	uint32_t	shmmax;
	uint32_t	shmmin;
	uint32_t	shmmni;
	uint32_t	shmseg;
	uint32_t	shmall;
};

#if defined(COMPAT_FREEBSD4) || defined(COMPAT_FREEBSD5) || \
    defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD7)
struct ipc_perm32_old {
	uint16_t	cuid;
	uint16_t	cgid;
	uint16_t	uid;
	uint16_t	gid;
	uint16_t	mode;
	uint16_t	seq;
	uint32_t	key;
};

struct semid_ds32_old {
	struct ipc_perm32_old sem_perm;
	uint32_t	__sem_base;
	unsigned short	sem_nsems;
	int32_t		sem_otime;
	int32_t		sem_pad1;
	int32_t		sem_ctime;
	int32_t		sem_pad2;
	int32_t		sem_pad3[4];
};

struct msqid_ds32_old {
	struct ipc_perm32_old msg_perm;
	uint32_t	__msg_first;
	uint32_t	__msg_last;
	uint32_t	msg_cbytes;
	uint32_t	msg_qnum;
	uint32_t	msg_qbytes;
	pid_t		msg_lspid;
	pid_t		msg_lrpid;
	int32_t		msg_stime;
	int32_t		msg_pad1;
	int32_t		msg_rtime;
	int32_t		msg_pad2;
	int32_t		msg_ctime;
	int32_t		msg_pad3;
	int32_t		msg_pad4[4];
};

struct shmid_ds32_old {
	struct ipc_perm32_old shm_perm;
	int32_t		shm_segsz;
	pid_t		shm_lpid;
	pid_t		shm_cpid;
	int16_t		shm_nattch;
	int32_t		shm_atime;
	int32_t		shm_dtime;
	int32_t		shm_ctime;
	uint32_t	shm_internal;
};

void	freebsd32_ipcperm_old_in(struct ipc_perm32_old *ip32,
	    struct ipc_perm *ip);
void	freebsd32_ipcperm_old_out(struct ipc_perm *ip,
	    struct ipc_perm32_old *ip32);
#endif

void	freebsd32_ipcperm_in(struct ipc_perm32 *ip32, struct ipc_perm *ip);
void	freebsd32_ipcperm_out(struct ipc_perm *ip, struct ipc_perm32 *ip32);

#endif /* !_COMPAT_FREEBSD32_FREEBSD32_IPC_H_ */
