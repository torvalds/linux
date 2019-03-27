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
 *	This product includes software developed by Adam Glass.
 * 4. The name of the Author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Adam Glass ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Adam Glass BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#define	_WANT_SYSVMSG_INTERNALS
#include <sys/msg.h>
#define	_WANT_SYSVSEM_INTERNALS
#define	_WANT_SEMUN
#include <sys/sem.h>
#define	_WANT_SYSVSHM_INTERNALS
#include <sys/shm.h>

#include <ctype.h>
#include <err.h>
#include <grp.h>
#include <kvm.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "ipc.h"

static int	signaled;
static int	errflg;
static int	rmverbose = 0;

static void
usage(void)
{

	fprintf(stderr,
	    "usage: ipcrm [-W] [-v[v]]\n"
	    "             [-q msqid] [-m shmid] [-s semid]\n"
	    "             [-Q msgkey] [-M shmkey] [-S semkey] ...\n");
	exit(1);
}

static int
msgrm(key_t key, int id)
{

	if (key == -1 || id == -1) {
		struct msqid_kernel *kxmsqids;
		size_t kxmsqids_len;
		int num;

		kget(X_MSGINFO, &msginfo, sizeof(msginfo));
		kxmsqids_len = sizeof(struct msqid_kernel) * msginfo.msgmni;
		kxmsqids = malloc(kxmsqids_len);
		kget(X_MSQIDS, kxmsqids, kxmsqids_len);
		num = msginfo.msgmni;
		while (num-- && !signaled)
			if (kxmsqids[num].u.msg_qbytes != 0) {
				id = IXSEQ_TO_IPCID(num,
					kxmsqids[num].u.msg_perm);
				if (msgctl(id, IPC_RMID, NULL) < 0) {
					if (rmverbose > 1)
						warn("msqid(%d): ", id);
					errflg++;
				} else
					if (rmverbose)
						printf(
						    "Removed %s %d\n",
						    IPC_TO_STRING('Q'),
						    id);
			}
		return signaled ? -1 : 0;       /* errors maybe handled above */
	}

	if (key) {
		id = msgget(key, 0);
		if (id == -1)
			return -1;
	}

	return msgctl(id, IPC_RMID, NULL);
}

static int
shmrm(key_t key, int id)
{

	if (key == -1 || id == -1) {
		struct shmid_kernel *kxshmids;
		size_t kxshmids_len;
		int num;

		kget(X_SHMINFO, &shminfo, sizeof(shminfo));
		kxshmids_len = sizeof(struct shmid_kernel) * shminfo.shmmni;
		kxshmids = malloc(kxshmids_len);
		kget(X_SHMSEGS, kxshmids, kxshmids_len);
		num = shminfo.shmmni;
		while (num-- && !signaled)
			if (kxshmids[num].u.shm_perm.mode & 0x0800) {
				id = IXSEQ_TO_IPCID(num,
					kxshmids[num].u.shm_perm);
				if (shmctl(id, IPC_RMID, NULL) < 0) {
					if (rmverbose > 1)
						warn("shmid(%d): ", id);
					errflg++;
				} else
					if (rmverbose)
						printf(
						    "Removed %s %d\n",
						    IPC_TO_STRING('M'),
						    id);
			}
		return signaled ? -1 : 0;       /* errors maybe handled above */
	}

	if (key) {
		id = shmget(key, 0, 0);
		if (id == -1)
			return -1;
	}

	return shmctl(id, IPC_RMID, NULL);
}

static int
semrm(key_t key, int id)
{
	union semun arg;

	if (key == -1 || id == -1) {
		struct semid_kernel *kxsema;
		size_t kxsema_len;
		int num;

		kget(X_SEMINFO, &seminfo, sizeof(seminfo));
		kxsema_len = sizeof(struct semid_kernel) * seminfo.semmni;
		kxsema = malloc(kxsema_len);
		kget(X_SEMA, kxsema, kxsema_len);
		num = seminfo.semmni;
		while (num-- && !signaled)
			if ((kxsema[num].u.sem_perm.mode & SEM_ALLOC) != 0) {
				id = IXSEQ_TO_IPCID(num,
					kxsema[num].u.sem_perm);
				if (semctl(id, 0, IPC_RMID, NULL) < 0) {
					if (rmverbose > 1)
						warn("semid(%d): ", id);
					errflg++;
				} else
					if (rmverbose)
						printf(
						    "Removed %s %d\n",
						    IPC_TO_STRING('S'),
						    id);
			}
		return signaled ? -1 : 0;       /* errors maybe handled above */
	}

	if (key) {
		id = semget(key, 0, 0);
		if (id == -1)
			return -1;
	}

	return semctl(id, 0, IPC_RMID, arg);
}

static void
not_configured(int signo __unused)
{

	signaled++;
}

int
main(int argc, char *argv[])
{
	int c, result, target_id;
	key_t target_key;

	while ((c = getopt(argc, argv, "q:m:s:Q:M:S:vWy")) != -1) {

		signaled = 0;
		switch (c) {
		case 'v':
			rmverbose++;
			break;
		case 'y':
			use_sysctl = 0;
			break;
		}
	}

	optind = 1;
	errflg = 0;
	signal(SIGSYS, not_configured);
	while ((c = getopt(argc, argv, "q:m:s:Q:M:S:vWy")) != -1) {

		signaled = 0;
		switch (c) {
		case 'q':
		case 'm':
		case 's':
			target_id = atoi(optarg);
			if (c == 'q')
				result = msgrm(0, target_id);
			else if (c == 'm')
				result = shmrm(0, target_id);
			else
				result = semrm(0, target_id);
			if (result < 0) {
				errflg++;
				if (!signaled)
					warn("%sid(%d): ",
					    IPC_TO_STR(toupper(c)), target_id);
				else
					warnx(
					    "%ss are not configured "
					    "in the running kernel",
					    IPC_TO_STRING(toupper(c)));
			}
			break;
		case 'Q':
		case 'M':
		case 'S':
			target_key = atol(optarg);
			if (target_key == IPC_PRIVATE) {
				warnx("can't remove private %ss",
				    IPC_TO_STRING(c));
				continue;
			}
			if (c == 'Q')
				result = msgrm(target_key, 0);
			else if (c == 'M')
				result = shmrm(target_key, 0);
			else
				result = semrm(target_key, 0);
			if (result < 0) {
				errflg++;
				if (!signaled)
					warn("%ss(%ld): ",
					    IPC_TO_STR(c), target_key);
				else
					warnx("%ss are not configured "
					    "in the running kernel",
					    IPC_TO_STRING(c));
			}
			break;
		case 'v':
		case 'y':
			/* Handled in other getopt() loop */
			break;
		case 'W':
			msgrm(-1, 0);
			shmrm(-1, 0);
			semrm(-1, 0);
			break;
		case ':':
			fprintf(stderr,
			    "option -%c requires an argument\n", optopt);
			usage();
		case '?':
			fprintf(stderr, "unrecognized option: -%c\n", optopt);
			usage();
		}
	}

	if (optind != argc) {
		fprintf(stderr, "unknown argument: %s\n", argv[optind]);
		usage();
	}
	exit(errflg);
}
