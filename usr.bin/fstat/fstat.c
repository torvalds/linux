/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2009 Stanislav Sedov <stas@FreeBSD.org>
 * Copyright (c) 1988, 1993
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/user.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>
#include <sys/queue.h>

#include <netinet/in.h>

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <libprocstat.h>
#include <limits.h>
#include <pwd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>

#include "functions.h"

static int 	fsflg,	/* show files on same filesystem as file(s) argument */
		pflg,	/* show files open by a particular pid */
		uflg;	/* show files open by a particular (effective) user */
static int 	checkfile; /* restrict to particular files or filesystems */
static int	nflg;	/* (numerical) display f.s. and rdev as dev_t */
static int	mflg;	/* include memory-mapped files */
static int	vflg;	/* be verbose */

typedef struct devs {
	struct devs	*next;
	uint64_t	fsid;
	uint64_t	ino;
	const char	*name;
} DEVS;

static DEVS *devs;
static char *memf, *nlistf;

static int	getfname(const char *filename);
static void	dofiles(struct procstat *procstat, struct kinfo_proc *p);
static void	print_access_flags(int flags);
static void	print_file_info(struct procstat *procstat,
    struct filestat *fst, const char *uname, const char *cmd, int pid);
static void	print_pipe_info(struct procstat *procstat,
    struct filestat *fst);
static void	print_pts_info(struct procstat *procstat,
    struct filestat *fst);
static void	print_sem_info(struct procstat *procstat,
    struct filestat *fst);
static void	print_shm_info(struct procstat *procstat,
    struct filestat *fst);
static void	print_socket_info(struct procstat *procstat,
    struct filestat *fst);
static void	print_vnode_info(struct procstat *procstat,
    struct filestat *fst);
static void	usage(void) __dead2;

int
do_fstat(int argc, char **argv)
{
	struct kinfo_proc *p;
	struct passwd *passwd;
	struct procstat *procstat;
	int arg, ch, what;
	int cnt, i;

	arg = 0;
	what = KERN_PROC_PROC;
	nlistf = memf = NULL;
	while ((ch = getopt(argc, argv, "fmnp:u:vN:M:")) != -1)
		switch((char)ch) {
		case 'f':
			fsflg = 1;
			break;
		case 'M':
			memf = optarg;
			break;
		case 'N':
			nlistf = optarg;
			break;
		case 'm':
			mflg = 1;
			break;
		case 'n':
			nflg = 1;
			break;
		case 'p':
			if (pflg++)
				usage();
			if (!isdigit(*optarg)) {
				warnx("-p requires a process id");
				usage();
			}
			what = KERN_PROC_PID;
			arg = atoi(optarg);
			break;
		case 'u':
			if (uflg++)
				usage();
			if (!(passwd = getpwnam(optarg)))
				errx(1, "%s: unknown uid", optarg);
			what = KERN_PROC_UID;
			arg = passwd->pw_uid;
			break;
		case 'v':
			vflg = 1;
			break;
		case '?':
		default:
			usage();
		}

	if (*(argv += optind)) {
		for (; *argv; ++argv) {
			if (getfname(*argv))
				checkfile = 1;
		}
		if (!checkfile)	/* file(s) specified, but none accessible */
			exit(1);
	}

	if (fsflg && !checkfile) {
		/* -f with no files means use wd */
		if (getfname(".") == 0)
			exit(1);
		checkfile = 1;
	}

	if (memf != NULL)
		procstat = procstat_open_kvm(nlistf, memf);
	else
		procstat = procstat_open_sysctl();
	if (procstat == NULL)
		errx(1, "procstat_open()");
	p = procstat_getprocs(procstat, what, arg, &cnt);
	if (p == NULL)
		errx(1, "procstat_getprocs()");

	/*
	 * Print header.
	 */
	if (nflg)
		printf("%s",
"USER     CMD          PID   FD  DEV    INUM       MODE SZ|DV R/W");
	else
		printf("%s",
"USER     CMD          PID   FD MOUNT      INUM MODE         SZ|DV R/W");
	if (checkfile && fsflg == 0)
		printf(" NAME\n");
	else
		putchar('\n');

	/*
	 * Go through the process list.
	 */
	for (i = 0; i < cnt; i++) {
		if (p[i].ki_stat == SZOMB)
			continue;
		dofiles(procstat, &p[i]);
	}
	procstat_freeprocs(procstat, p);
	procstat_close(procstat);
	return (0);
}

static void
dofiles(struct procstat *procstat, struct kinfo_proc *kp)
{
	const char *cmd;
	const char *uname;
	struct filestat *fst;
	struct filestat_list *head;
	int pid;

	uname = user_from_uid(kp->ki_uid, 0);
	pid = kp->ki_pid;
	cmd = kp->ki_comm;

	head = procstat_getfiles(procstat, kp, mflg);
	if (head == NULL)
		return;
	STAILQ_FOREACH(fst, head, next)
		print_file_info(procstat, fst, uname, cmd, pid);
	procstat_freefiles(procstat, head);
}


static void
print_file_info(struct procstat *procstat, struct filestat *fst,
    const char *uname, const char *cmd, int pid)
{
	struct vnstat vn;
	DEVS *d;
	const char *filename;
	int error, fsmatch = 0;
	char errbuf[_POSIX2_LINE_MAX];

	filename = NULL;
	if (checkfile != 0) {
		if (fst->fs_type != PS_FST_TYPE_VNODE &&
		    fst->fs_type != PS_FST_TYPE_FIFO)
			return;
		error = procstat_get_vnode_info(procstat, fst, &vn, errbuf);
		if (error != 0)
			return;

		for (d = devs; d != NULL; d = d->next)
			if (d->fsid == vn.vn_fsid) {
				fsmatch = 1;
				if (d->ino == vn.vn_fileid) {
					filename = d->name;
					break;
				}
			}
		if (fsmatch == 0 || (filename == NULL && fsflg == 0))
			return;
	}

	/*
	 * Print entry prefix.
	 */
	printf("%-8.8s %-10s %5d", uname, cmd, pid);
	if (fst->fs_uflags & PS_FST_UFLAG_TEXT)
		printf(" text");
	else if (fst->fs_uflags & PS_FST_UFLAG_CDIR)
		printf("   wd");
	else if (fst->fs_uflags & PS_FST_UFLAG_RDIR)
		printf(" root");
	else if (fst->fs_uflags & PS_FST_UFLAG_TRACE)
		printf("   tr");
	else if (fst->fs_uflags & PS_FST_UFLAG_MMAP)
		printf(" mmap");
	else if (fst->fs_uflags & PS_FST_UFLAG_JAIL)
		printf(" jail");
	else if (fst->fs_uflags & PS_FST_UFLAG_CTTY)
		printf(" ctty");
	else
		printf(" %4d", fst->fs_fd);

	/*
	 * Print type-specific data.
	 */
	switch (fst->fs_type) {
	case PS_FST_TYPE_FIFO:
	case PS_FST_TYPE_VNODE:
		print_vnode_info(procstat, fst);
		break;
	case PS_FST_TYPE_SOCKET:
		print_socket_info(procstat, fst);
		break;
	case PS_FST_TYPE_PIPE:
		print_pipe_info(procstat, fst);
		break;
	case PS_FST_TYPE_PTS:
		print_pts_info(procstat, fst);
		break;
	case PS_FST_TYPE_SHM:
		print_shm_info(procstat, fst);
		break;
	case PS_FST_TYPE_SEM:
		print_sem_info(procstat, fst);
		break;
	case PS_FST_TYPE_DEV:
		break;
	default:	
		if (vflg)
			fprintf(stderr,
			    "unknown file type %d for file %d of pid %d\n",
			    fst->fs_type, fst->fs_fd, pid);
	}
	if (filename && !fsflg)
		printf("  %s", filename);
	putchar('\n');
}

static void
print_socket_info(struct procstat *procstat, struct filestat *fst)
{
	static const char *stypename[] = {
		"unused",	/* 0 */
		"stream",	/* 1 */
		"dgram",	/* 2 */
		"raw",		/* 3 */
		"rdm",		/* 4 */
		"seqpak"	/* 5 */
	};
#define STYPEMAX 5
	struct sockstat sock;
	struct protoent *pe;
	char errbuf[_POSIX2_LINE_MAX];
	int error;
	static int isopen;

	error = procstat_get_socket_info(procstat, fst, &sock, errbuf);
	if (error != 0) {
		printf("* error");
		return;
	}
	if (sock.type > STYPEMAX)
		printf("* %s ?%d", sock.dname, sock.type);
	else
		printf("* %s %s", sock.dname, stypename[sock.type]);

	/*
	 * protocol specific formatting
	 *
	 * Try to find interesting things to print.  For tcp, the interesting
	 * thing is the address of the tcpcb, for udp and others, just the
	 * inpcb (socket pcb).  For unix domain, its the address of the socket
	 * pcb and the address of the connected pcb (if connected).  Otherwise
	 * just print the protocol number and address of the socket itself.
	 * The idea is not to duplicate netstat, but to make available enough
	 * information for further analysis.
	 */
	switch (sock.dom_family) {
	case AF_INET:
	case AF_INET6:
		if (!isopen)
			setprotoent(++isopen);
		if ((pe = getprotobynumber(sock.proto)) != NULL)
			printf(" %s", pe->p_name);
		else
			printf(" %d", sock.proto);
		if (sock.proto == IPPROTO_TCP ) {
			if (sock.inp_ppcb != 0)
				printf(" %lx", (u_long)sock.inp_ppcb);
		}
		else if (sock.so_pcb != 0)
			printf(" %lx", (u_long)sock.so_pcb);
		break;
	case AF_UNIX:
		/* print address of pcb and connected pcb */
		if (sock.so_pcb != 0) {
			printf(" %lx", (u_long)sock.so_pcb);
			if (sock.unp_conn) {
				char shoconn[4], *cp;

				cp = shoconn;
				if (!(sock.so_rcv_sb_state & SBS_CANTRCVMORE))
					*cp++ = '<';
				*cp++ = '-';
				if (!(sock.so_snd_sb_state & SBS_CANTSENDMORE))
					*cp++ = '>';
				*cp = '\0';
				printf(" %s %lx", shoconn,
				    (u_long)sock.unp_conn);
                        }
		}
		break;
	default:
		/* print protocol number and socket address */
		printf(" %d %lx", sock.proto, (u_long)sock.so_addr);
	}
}

static void
print_pipe_info(struct procstat *procstat, struct filestat *fst)
{
	struct pipestat ps;
	char errbuf[_POSIX2_LINE_MAX];
	int error;

	error = procstat_get_pipe_info(procstat, fst, &ps, errbuf);
	if (error != 0) {
		printf("* error");
		return;
	}
	printf("* pipe %8lx <-> %8lx", (u_long)ps.addr, (u_long)ps.peer);
	printf(" %6zd", ps.buffer_cnt);
	print_access_flags(fst->fs_fflags);
}

static void
print_pts_info(struct procstat *procstat, struct filestat *fst)
{
	struct ptsstat pts;
	char errbuf[_POSIX2_LINE_MAX];
	int error;

	error = procstat_get_pts_info(procstat, fst, &pts, errbuf);
	if (error != 0) {
		printf("* error");
		return;
	}
	printf("* pseudo-terminal master ");
	if (nflg || !*pts.devname) {
		printf("%#10jx", (uintmax_t)pts.dev);
	} else {
		printf("%10s", pts.devname);
	}
	print_access_flags(fst->fs_fflags);
}

static void
print_sem_info(struct procstat *procstat, struct filestat *fst)
{
	struct semstat sem;
	char errbuf[_POSIX2_LINE_MAX];
	char mode[15];
	int error;

	error = procstat_get_sem_info(procstat, fst, &sem, errbuf);
	if (error != 0) {
		printf("* error");
		return;
	}
	if (nflg) {
		printf("             ");
		(void)snprintf(mode, sizeof(mode), "%o", sem.mode);
	} else {
		printf(" %-15s", fst->fs_path != NULL ? fst->fs_path : "-");
		strmode(sem.mode, mode);
	}
	printf(" %10s %6u", mode, sem.value);
	print_access_flags(fst->fs_fflags);
}

static void
print_shm_info(struct procstat *procstat, struct filestat *fst)
{
	struct shmstat shm;
	char errbuf[_POSIX2_LINE_MAX];
	char mode[15];
	int error;

	error = procstat_get_shm_info(procstat, fst, &shm, errbuf);
	if (error != 0) {
		printf("* error");
		return;
	}
	if (nflg) {
		printf("             ");
		(void)snprintf(mode, sizeof(mode), "%o", shm.mode);
	} else {
		printf(" %-15s", fst->fs_path != NULL ? fst->fs_path : "-");
		strmode(shm.mode, mode);
	}
	printf(" %10s %6ju", mode, shm.size);
	print_access_flags(fst->fs_fflags);
}

static void
print_vnode_info(struct procstat *procstat, struct filestat *fst)
{
	struct vnstat vn;
	char errbuf[_POSIX2_LINE_MAX];
	char mode[15];
	const char *badtype;
	int error;

	badtype = NULL;
	error = procstat_get_vnode_info(procstat, fst, &vn, errbuf);
	if (error != 0)
		badtype = errbuf;
	else if (vn.vn_type == PS_FST_VTYPE_VBAD)
		badtype = "bad";
	else if (vn.vn_type == PS_FST_VTYPE_VNON)
		badtype = "none";
	if (badtype != NULL) {
		printf(" -         -  %10s    -", badtype);
		return;
	}

	if (nflg)
		printf(" %#5jx", (uintmax_t)vn.vn_fsid);
	else if (vn.vn_mntdir != NULL)
		(void)printf(" %-8s", vn.vn_mntdir);

	/*
	 * Print access mode.
	 */
	if (nflg)
		(void)snprintf(mode, sizeof(mode), "%o", vn.vn_mode);
	else {
		strmode(vn.vn_mode, mode);
	}
	(void)printf(" %6jd %10s", (intmax_t)vn.vn_fileid, mode);

	if (vn.vn_type == PS_FST_VTYPE_VBLK || vn.vn_type == PS_FST_VTYPE_VCHR) {
		if (nflg || !*vn.vn_devname)
			printf(" %#6jx", (uintmax_t)vn.vn_dev);
		else {
			printf(" %6s", vn.vn_devname);
		}
	} else
		printf(" %6ju", (uintmax_t)vn.vn_size);
	print_access_flags(fst->fs_fflags);
}

static void
print_access_flags(int flags)
{
	char rw[3];

	rw[0] = '\0';
	if (flags & PS_FST_FFLAG_READ)
		strcat(rw, "r");
	if (flags & PS_FST_FFLAG_WRITE)
		strcat(rw, "w");
	printf(" %2s", rw);
}

int
getfname(const char *filename)
{
	struct stat statbuf;
	DEVS *cur;

	if (stat(filename, &statbuf)) {
		warn("%s", filename);
		return (0);
	}
	if ((cur = malloc(sizeof(DEVS))) == NULL)
		err(1, NULL);
	cur->next = devs;
	devs = cur;

	cur->ino = statbuf.st_ino;
	cur->fsid = statbuf.st_dev;
	cur->name = filename;
	return (1);
}

static void
usage(void)
{
	(void)fprintf(stderr,
 "usage: fstat [-fmnv] [-M core] [-N system] [-p pid] [-u user] [file ...]\n");
	exit(1);
}
