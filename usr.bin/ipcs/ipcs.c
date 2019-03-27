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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/proc.h>
#define	_WANT_SYSVMSG_INTERNALS
#include <sys/msg.h>
#define	_WANT_SYSVSEM_INTERNALS
#include <sys/sem.h>
#define	_WANT_SYSVSHM_INTERNALS
#include <sys/shm.h>

#include <err.h>
#include <fcntl.h>
#include <grp.h>
#include <kvm.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ipc.h"

char   *fmt_perm(u_short);
void	cvt_time(time_t, char *);
void	usage(void);
uid_t	user2uid(char *username);

void	print_kmsqtotal(struct msginfo msginfo);
void	print_kmsqheader(int option);
void	print_kmsqptr(int i, int option, struct msqid_kernel *kmsqptr);
void	print_kshmtotal(struct shminfo shminfo);
void	print_kshmheader(int option);
void	print_kshmptr(int i, int option, struct shmid_kernel *kshmptr);
void	print_ksemtotal(struct seminfo seminfo);
void	print_ksemheader(int option);
void	print_ksemptr(int i, int option, struct semid_kernel *ksemaptr);

char   *
fmt_perm(u_short mode)
{
	static char buffer[100];

	buffer[0] = '-';
	buffer[1] = '-';
	buffer[2] = ((mode & 0400) ? 'r' : '-');
	buffer[3] = ((mode & 0200) ? 'w' : '-');
	buffer[4] = ((mode & 0100) ? 'a' : '-');
	buffer[5] = ((mode & 0040) ? 'r' : '-');
	buffer[6] = ((mode & 0020) ? 'w' : '-');
	buffer[7] = ((mode & 0010) ? 'a' : '-');
	buffer[8] = ((mode & 0004) ? 'r' : '-');
	buffer[9] = ((mode & 0002) ? 'w' : '-');
	buffer[10] = ((mode & 0001) ? 'a' : '-');
	buffer[11] = '\0';
	return (&buffer[0]);
}

void
cvt_time(time_t t, char *buf)
{
	struct tm *tm;

	if (t == 0) {
		strcpy(buf, "no-entry");
	} else {
		tm = localtime(&t);
		sprintf(buf, "%2d:%02d:%02d",
			tm->tm_hour, tm->tm_min, tm->tm_sec);
	}
}

#define BIGGEST		1
#define CREATOR		2
#define OUTSTANDING	4
#define PID		8
#define TIME		16

int
main(int argc, char *argv[])
{
	int     display = SHMINFO | MSGINFO | SEMINFO;
	int     option = 0;
	char   *core = NULL, *user = NULL, *namelist = NULL;
	char	kvmoferr[_POSIX2_LINE_MAX];  /* Error buf for kvm_openfiles. */
	int     i;
	u_long  shmidx;
	uid_t   uid = 0;

	while ((i = getopt(argc, argv, "MmQqSsabC:cN:optTu:y")) != -1)
		switch (i) {
		case 'a':
			option |= BIGGEST | CREATOR | OUTSTANDING | PID | TIME;
			break;
		case 'b':
			option |= BIGGEST;
			break;
		case 'C':
			core = optarg;
			break;
		case 'c':
			option |= CREATOR;
			break;
		case 'M':
			display = SHMTOTAL;
			break;
		case 'm':
			display = SHMINFO;
			break;
		case 'N':
			namelist = optarg;
			break;
		case 'o':
			option |= OUTSTANDING;
			break;
		case 'p':
			option |= PID;
			break;
		case 'Q':
			display = MSGTOTAL;
			break;
		case 'q':
			display = MSGINFO;
			break;
		case 'S':
			display = SEMTOTAL;
			break;
		case 's':
			display = SEMINFO;
			break;
		case 'T':
			display = SHMTOTAL | MSGTOTAL | SEMTOTAL;
			break;
		case 't':
			option |= TIME;
			break;
		case 'u':
			user = optarg;
			uid = user2uid(user);
			break;
		case 'y':
			use_sysctl = 0;
			break;
		default:
			usage();
		}

	/*
	 * If paths to the exec file or core file were specified, we
	 * aren't operating on the running kernel, so we can't use
	 * sysctl.
	 */
	if (namelist != NULL || core != NULL)
		use_sysctl = 0;

	if (!use_sysctl) {
		kd = kvm_openfiles(namelist, core, NULL, O_RDONLY, kvmoferr);
		if (kd == NULL)
			errx(1, "kvm_openfiles: %s", kvmoferr);
		switch (kvm_nlist(kd, symbols)) {
		case 0:
			break;
		case -1:
			errx(1, "unable to read kernel symbol table");
		default:
			break;
		}
	}

	kget(X_MSGINFO, &msginfo, sizeof(msginfo));
	if (display & (MSGINFO | MSGTOTAL)) {
		if (display & MSGTOTAL)
			print_kmsqtotal(msginfo);

		if (display & MSGINFO) {
			struct msqid_kernel *kxmsqids;
			size_t kxmsqids_len;

			kxmsqids_len =
			    sizeof(struct msqid_kernel) * msginfo.msgmni;
			kxmsqids = malloc(kxmsqids_len);
			kget(X_MSQIDS, kxmsqids, kxmsqids_len);

			print_kmsqheader(option);

			for (i = 0; i < msginfo.msgmni; i += 1) {
				if (kxmsqids[i].u.msg_qbytes != 0) {
					if (user &&
					    uid != kxmsqids[i].u.msg_perm.uid)
						continue;

					print_kmsqptr(i, option, &kxmsqids[i]);
				}

			}

			printf("\n");
		}
	}

	kget(X_SHMINFO, &shminfo, sizeof(shminfo));
	if (display & (SHMINFO | SHMTOTAL)) {

		if (display & SHMTOTAL)
			print_kshmtotal(shminfo);

		if (display & SHMINFO) {
			struct shmid_kernel *kxshmids;
			size_t kxshmids_len;

			kxshmids_len =
			    sizeof(struct shmid_kernel) * shminfo.shmmni;
			kxshmids = malloc(kxshmids_len);
			kget(X_SHMSEGS, kxshmids, kxshmids_len);

			print_kshmheader(option);

			for (shmidx = 0; shmidx < shminfo.shmmni; shmidx += 1) {
				if (kxshmids[shmidx].u.shm_perm.mode & 0x0800) {
					if (user &&
					    uid != kxshmids[shmidx].u.shm_perm.uid)
						continue;

					print_kshmptr(shmidx, option, &kxshmids[shmidx]);
				}
			}
			printf("\n");
		}
	}

	kget(X_SEMINFO, &seminfo, sizeof(seminfo));
	if (display & (SEMINFO | SEMTOTAL)) {
		struct semid_kernel *kxsema;
		size_t kxsema_len;

		if (display & SEMTOTAL)
			print_ksemtotal(seminfo);

		if (display & SEMINFO) {
			kxsema_len =
			    sizeof(struct semid_kernel) * seminfo.semmni;
			kxsema = malloc(kxsema_len);
			kget(X_SEMA, kxsema, kxsema_len);

			print_ksemheader(option);

			for (i = 0; i < seminfo.semmni; i += 1) {
				if ((kxsema[i].u.sem_perm.mode & SEM_ALLOC)
				    != 0) {
					if (user &&
					    uid != kxsema[i].u.sem_perm.uid)
						continue;

					print_ksemptr(i, option, &kxsema[i]);

				}
			}

			printf("\n");
		}
	}

	if (!use_sysctl)
		kvm_close(kd);

	exit(0);
}

void
print_kmsqtotal(struct msginfo local_msginfo)
{

	printf("msginfo:\n");
	printf("\tmsgmax: %12d\t(max characters in a message)\n",
	    local_msginfo.msgmax);
	printf("\tmsgmni: %12d\t(# of message queues)\n",
	    local_msginfo.msgmni);
	printf("\tmsgmnb: %12d\t(max characters in a message queue)\n",
	    local_msginfo.msgmnb);
	printf("\tmsgtql: %12d\t(max # of messages in system)\n",
	    local_msginfo.msgtql);
	printf("\tmsgssz: %12d\t(size of a message segment)\n",
	    local_msginfo.msgssz);
	printf("\tmsgseg: %12d\t(# of message segments in system)\n\n",
	    local_msginfo.msgseg);
}

void print_kmsqheader(int option)
{

	printf("Message Queues:\n");
	printf("T %12s %12s %-11s %-8s %-8s",
	    "ID", "KEY", "MODE", "OWNER", "GROUP");
	if (option & CREATOR)
		printf(" %-8s %-8s", "CREATOR", "CGROUP");
	if (option & OUTSTANDING)
		printf(" %20s %20s", "CBYTES", "QNUM");
	if (option & BIGGEST)
		printf(" %20s", "QBYTES");
	if (option & PID)
		printf(" %12s %12s", "LSPID", "LRPID");
	if (option & TIME)
		printf(" %-8s %-8s %-8s", "STIME", "RTIME", "CTIME");
	printf("\n");
}

void
print_kmsqptr(int i, int option, struct msqid_kernel *kmsqptr)
{
	char    stime_buf[100], rtime_buf[100], ctime_buf[100];

	cvt_time(kmsqptr->u.msg_stime, stime_buf);
	cvt_time(kmsqptr->u.msg_rtime, rtime_buf);
	cvt_time(kmsqptr->u.msg_ctime, ctime_buf);

	printf("q %12d %12d %s %-8s %-8s",
	    IXSEQ_TO_IPCID(i, kmsqptr->u.msg_perm),
	    (int)kmsqptr->u.msg_perm.key,
	    fmt_perm(kmsqptr->u.msg_perm.mode),
	    user_from_uid(kmsqptr->u.msg_perm.uid, 0),
	    group_from_gid(kmsqptr->u.msg_perm.gid, 0));

	if (option & CREATOR)
		printf(" %-8s %-8s",
		    user_from_uid(kmsqptr->u.msg_perm.cuid, 0),
		    group_from_gid(kmsqptr->u.msg_perm.cgid, 0));

	if (option & OUTSTANDING)
		printf(" %12lu %12lu",
		    kmsqptr->u.msg_cbytes,
		    kmsqptr->u.msg_qnum);

	if (option & BIGGEST)
		printf(" %20lu", kmsqptr->u.msg_qbytes);

	if (option & PID)
		printf(" %12d %12d",
		    kmsqptr->u.msg_lspid,
		    kmsqptr->u.msg_lrpid);

	if (option & TIME)
		printf(" %s %s %s",
		    stime_buf,
		    rtime_buf,
		    ctime_buf);

	printf("\n");
}

void
print_kshmtotal(struct shminfo local_shminfo)
{

	printf("shminfo:\n");
	printf("\tshmmax: %12lu\t(max shared memory segment size)\n",
	    local_shminfo.shmmax);
	printf("\tshmmin: %12lu\t(min shared memory segment size)\n",
	    local_shminfo.shmmin);
	printf("\tshmmni: %12lu\t(max number of shared memory identifiers)\n",
	    local_shminfo.shmmni);
	printf("\tshmseg: %12lu\t(max shared memory segments per process)\n",
	    local_shminfo.shmseg);
	printf("\tshmall: %12lu\t(max amount of shared memory in pages)\n\n",
	    local_shminfo.shmall);
}

void
print_kshmheader(int option)
{

	printf("Shared Memory:\n");
	printf("T %12s %12s %-11s %-8s %-8s",
	    "ID", "KEY", "MODE", "OWNER", "GROUP");
	if (option & CREATOR)
		printf(" %-8s %-8s", "CREATOR", "CGROUP");
	if (option & OUTSTANDING)
		printf(" %12s", "NATTCH");
	if (option & BIGGEST)
		printf(" %12s", "SEGSZ");
	if (option & PID)
		printf(" %12s %12s", "CPID", "LPID");
	if (option & TIME)
		printf(" %-8s %-8s %-8s", "ATIME", "DTIME", "CTIME");
	printf("\n");
}

void
print_kshmptr(int i, int option, struct shmid_kernel *kshmptr)
{
	char    atime_buf[100], dtime_buf[100], ctime_buf[100];

	cvt_time(kshmptr->u.shm_atime, atime_buf);
	cvt_time(kshmptr->u.shm_dtime, dtime_buf);
	cvt_time(kshmptr->u.shm_ctime, ctime_buf);

	printf("m %12d %12d %s %-8s %-8s",
	    IXSEQ_TO_IPCID(i, kshmptr->u.shm_perm),
	    (int)kshmptr->u.shm_perm.key,
	    fmt_perm(kshmptr->u.shm_perm.mode),
	    user_from_uid(kshmptr->u.shm_perm.uid, 0),
	    group_from_gid(kshmptr->u.shm_perm.gid, 0));

	if (option & CREATOR)
		printf(" %-8s %-8s",
		    user_from_uid(kshmptr->u.shm_perm.cuid, 0),
		    group_from_gid(kshmptr->u.shm_perm.cgid, 0));

	if (option & OUTSTANDING)
		printf(" %12d",
		    kshmptr->u.shm_nattch);

	if (option & BIGGEST)
		printf(" %12zu",
		    kshmptr->u.shm_segsz);

	if (option & PID)
		printf(" %12d %12d",
		    kshmptr->u.shm_cpid,
		    kshmptr->u.shm_lpid);

	if (option & TIME)
		printf(" %s %s %s",
		    atime_buf,
		    dtime_buf,
		    ctime_buf);

	printf("\n");
}

void
print_ksemtotal(struct seminfo local_seminfo)
{

	printf("seminfo:\n");
	printf("\tsemmni: %12d\t(# of semaphore identifiers)\n",
	    local_seminfo.semmni);
	printf("\tsemmns: %12d\t(# of semaphores in system)\n",
	    local_seminfo.semmns);
	printf("\tsemmnu: %12d\t(# of undo structures in system)\n",
	    local_seminfo.semmnu);
	printf("\tsemmsl: %12d\t(max # of semaphores per id)\n",
	    local_seminfo.semmsl);
	printf("\tsemopm: %12d\t(max # of operations per semop call)\n",
	    local_seminfo.semopm);
	printf("\tsemume: %12d\t(max # of undo entries per process)\n",
	    local_seminfo.semume);
	printf("\tsemusz: %12d\t(size in bytes of undo structure)\n",
	    local_seminfo.semusz);
	printf("\tsemvmx: %12d\t(semaphore maximum value)\n",
	    local_seminfo.semvmx);
	printf("\tsemaem: %12d\t(adjust on exit max value)\n\n",
	    local_seminfo.semaem);
}

void
print_ksemheader(int option)
{

	printf("Semaphores:\n");
	printf("T %12s %12s %-11s %-8s %-8s",
	    "ID", "KEY", "MODE", "OWNER", "GROUP");
	if (option & CREATOR)
		printf(" %-8s %-8s", "CREATOR", "CGROUP");
	if (option & BIGGEST)
		printf(" %12s", "NSEMS");
	if (option & TIME)
		printf(" %-8s %-8s", "OTIME", "CTIME");
	printf("\n");
}

void
print_ksemptr(int i, int option, struct semid_kernel *ksemaptr)
{
	char    ctime_buf[100], otime_buf[100];

	cvt_time(ksemaptr->u.sem_otime, otime_buf);
	cvt_time(ksemaptr->u.sem_ctime, ctime_buf);

	printf("s %12d %12d %s %-8s %-8s",
	    IXSEQ_TO_IPCID(i, ksemaptr->u.sem_perm),
	    (int)ksemaptr->u.sem_perm.key,
	    fmt_perm(ksemaptr->u.sem_perm.mode),
	    user_from_uid(ksemaptr->u.sem_perm.uid, 0),
	    group_from_gid(ksemaptr->u.sem_perm.gid, 0));

	if (option & CREATOR)
		printf(" %-8s %-8s",
		    user_from_uid(ksemaptr->u.sem_perm.cuid, 0),
		    group_from_gid(ksemaptr->u.sem_perm.cgid, 0));

	if (option & BIGGEST)
		printf(" %12d",
		    ksemaptr->u.sem_nsems);

	if (option & TIME)
		printf(" %s %s",
		    otime_buf,
		    ctime_buf);

	printf("\n");
}

uid_t 
user2uid(char *username)
{
	struct passwd *pwd;
	uid_t uid;
	char *r;

	uid = strtoul(username, &r, 0);
	if (!*r && r != username)
		return (uid);
	if ((pwd = getpwnam(username)) == NULL)
		errx(1, "getpwnam failed: No such user");
	endpwent();
	return (pwd->pw_uid);
}

void
usage(void)
{

	fprintf(stderr,
	    "usage: "
	    "ipcs [-abcmopqstyMQST] [-C corefile] [-N namelist] [-u user]\n");
	exit(1);
}
