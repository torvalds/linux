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
 *
 * $FreeBSD$
 */

/* Part of struct nlist symbols[] */
#define X_SEMA		0
#define X_SEMINFO	1
#define X_MSGINFO	2
#define X_MSQIDS	3
#define X_SHMINFO	4
#define X_SHMSEGS	5

#define	SHMINFO		1
#define	SHMTOTAL	2
#define	MSGINFO		4
#define	MSGTOTAL	8
#define	SEMINFO		16
#define	SEMTOTAL	32

#define IPC_TO_STR(x) (x == 'Q' ? "msq" : (x == 'M' ? "shm" : "sem"))
#define IPC_TO_STRING(x) (x == 'Q' ? "message queue" : \
	    (x == 'M' ? "shared memory segment" : "semaphore"))

/* SysCtlGatherStruct structure. */
struct scgs_vector {
	const char *sysctl;
	size_t offset;
	size_t size;
};

void	kget(int idx, void *addr, size_t size);
void	sysctlgatherstruct(void *addr, size_t size, struct scgs_vector *vec);

extern int use_sysctl;
extern struct nlist symbols[];
extern kvm_t *kd;

extern struct semid_kernel	*sema;
extern struct msqid_kernel	*msqids;
extern struct shmid_kernel	*shmsegs;
extern struct seminfo		 seminfo;
extern struct msginfo		 msginfo;
extern struct shminfo		 shminfo;
