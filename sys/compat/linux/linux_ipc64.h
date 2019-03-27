/*-
 * Copyright (c) 2002 Maxim Sobolev <sobomax@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
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
 *
 * $FreeBSD$
 */

#ifndef _LINUX_IPC64_H_
#define	_LINUX_IPC64_H_

/*
 * The generic ipc64_perm structure.
 * Note extra padding because this structure is passed back and forth
 * between kernel and user space.
 *
 * Pad space is left for:
 * - 32-bit mode_t on architectures that only had 16 bit
 * - 32-bit seq
 * - 2 miscellaneous 32-bit values
 */
struct l_ipc64_perm
{
	l_key_t		key;
	l_uid_t		uid;
	l_gid_t		gid;
	l_uid_t		cuid;
	l_gid_t		cgid;
	l_mode_t	mode;
			/* pad if mode_t is ushort: */
	unsigned char	__pad1[sizeof(l_int) - sizeof(l_mode_t)];
	l_ushort	seq;
	l_ushort	__pad2;
	l_ulong		__unused1;
	l_ulong		__unused2;
};

/*
 * The generic msqid64_ds structure fro x86 architecture.
 * Note extra padding because this structure is passed back and forth
 * between kernel and user space.
 *
 * Pad space is left for:
 * - 64-bit time_t to solve y2038 problem
 * - 2 miscellaneous 32-bit values
 */

struct l_msqid64_ds {
	struct l_ipc64_perm msg_perm;
	l_time_t	msg_stime;	/* last msgsnd time */
#if !defined(__LP64__) || defined(COMPAT_LINUX32)
	l_ulong		__unused1;
#endif
	l_time_t	msg_rtime;	/* last msgrcv time */
#if !defined(__LP64__) || defined(COMPAT_LINUX32)
	l_ulong		__unused2;
#endif
	l_time_t	msg_ctime;	/* last change time */
#if !defined(__LP64__) || defined(COMPAT_LINUX32)
	l_ulong		__unused3;
#endif
	l_ulong		msg_cbytes;	/* current number of bytes on queue */
	l_ulong		msg_qnum;	/* number of messages in queue */
	l_ulong		msg_qbytes;	/* max number of bytes on queue */
	l_pid_t		msg_lspid;	/* pid of last msgsnd */
	l_pid_t		msg_lrpid;	/* last receive pid */
	l_ulong		__unused4;
	l_ulong		__unused5;
};

/*
 * The generic semid64_ds structure for x86 architecture.
 * Note extra padding because this structure is passed back and forth
 * between kernel and user space.
 *
 * Pad space is left for:
 * - 64-bit time_t to solve y2038 problem
 * - 2 miscellaneous 32-bit values
 */

struct l_semid64_ds {
	struct l_ipc64_perm sem_perm;	/* permissions */
	l_time_t	sem_otime;	/* last semop time */
	l_ulong		__unused1;
	l_time_t	sem_ctime;	/* last change time */
	l_ulong		__unused2;
	l_ulong		sem_nsems;	/* no. of semaphores in array */
	l_ulong		__unused3;
	l_ulong		__unused4;
};

/*
 * The generic shmid64_ds structure for x86 architecture.
 * Note extra padding because this structure is passed back and forth
 * between kernel and user space.
 *
 * Pad space is left for:
 * - 64-bit time_t to solve y2038 problem
 * - 2 miscellaneous 32-bit values
 */

struct l_shmid64_ds {
	struct l_ipc64_perm shm_perm;	/* operation perms */
	l_size_t	shm_segsz;	/* size of segment (bytes) */
	l_time_t	shm_atime;	/* last attach time */
#if !defined(__LP64__) || defined(COMPAT_LINUX32)
	l_ulong		__unused1;
#endif
	l_time_t	shm_dtime;	/* last detach time */
#if !defined(__LP64__) || defined(COMPAT_LINUX32)
	l_ulong		__unused2;
#endif
	l_time_t	shm_ctime;	/* last change time */
#if !defined(__LP64__) || defined(COMPAT_LINUX32)
	l_ulong		__unused3;
#endif
	l_pid_t		shm_cpid;	/* pid of creator */
	l_pid_t		shm_lpid;	/* pid of last operator */
	l_ulong		shm_nattch;	/* no. of current attaches */
	l_ulong		__unused4;
	l_ulong		__unused5;
};

struct l_shminfo64 {
	l_ulong		shmmax;
	l_ulong		shmmin;
	l_ulong		shmmni;
	l_ulong		shmseg;
	l_ulong		shmall;
	l_ulong		__unused1;
	l_ulong		__unused2;
	l_ulong		__unused3;
	l_ulong		__unused4;
};

#endif /* !LINUX_IPC64_H_ */
