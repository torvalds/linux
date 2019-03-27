/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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

#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1988, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
#if 0
static char sccsid[] = "@(#)kdump.c	8.1 (Berkeley) 6/6/93";
#endif
#endif /* not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#define _WANT_KERNEL_ERRNO
#ifdef __LP64__
#define	_WANT_KEVENT32
#endif
#define	_WANT_FREEBSD11_KEVENT
#include <sys/param.h>
#include <sys/capsicum.h>
#include <sys/errno.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/event.h>
#include <sys/ktrace.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysent.h>
#include <sys/umtx.h>
#include <sys/un.h>
#include <sys/queue.h>
#include <sys/wait.h>
#ifdef WITH_CASPER
#include <sys/nv.h>
#endif
#include <arpa/inet.h>
#include <netinet/in.h>
#include <ctype.h>
#include <capsicum_helpers.h>
#include <err.h>
#include <grp.h>
#include <inttypes.h>
#include <locale.h>
#include <netdb.h>
#include <nl_types.h>
#include <pwd.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysdecode.h>
#include <time.h>
#include <unistd.h>
#include <vis.h>
#include "ktrace.h"

#ifdef WITH_CASPER
#include <libcasper.h>

#include <casper/cap_grp.h>
#include <casper/cap_pwd.h>
#endif

int fetchprocinfo(struct ktr_header *, u_int *);
u_int findabi(struct ktr_header *);
int fread_tail(void *, int, int);
void dumpheader(struct ktr_header *, u_int);
void ktrsyscall(struct ktr_syscall *, u_int);
void ktrsysret(struct ktr_sysret *, u_int);
void ktrnamei(char *, int);
void hexdump(char *, int, int);
void visdump(char *, int, int);
void ktrgenio(struct ktr_genio *, int);
void ktrpsig(struct ktr_psig *);
void ktrcsw(struct ktr_csw *);
void ktrcsw_old(struct ktr_csw_old *);
void ktruser(int, void *);
void ktrcaprights(cap_rights_t *);
void ktritimerval(struct itimerval *it);
void ktrsockaddr(struct sockaddr *);
void ktrstat(struct stat *);
void ktrstruct(char *, size_t);
void ktrcapfail(struct ktr_cap_fail *);
void ktrfault(struct ktr_fault *);
void ktrfaultend(struct ktr_faultend *);
void ktrkevent(struct kevent *);
void ktrstructarray(struct ktr_struct_array *, size_t);
void usage(void);

#define	TIMESTAMP_NONE		0x0
#define	TIMESTAMP_ABSOLUTE	0x1
#define	TIMESTAMP_ELAPSED	0x2
#define	TIMESTAMP_RELATIVE	0x4

static bool abiflag, decimal, fancy = true, resolv, suppressdata, syscallno,
    tail, threads;
static int timestamp, maxdata;
static const char *tracefile = DEF_TRACEFILE;
static struct ktr_header ktr_header;

#define TIME_FORMAT	"%b %e %T %Y"
#define eqs(s1, s2)	(strcmp((s1), (s2)) == 0)

#define	print_number64(first,i,n,c) do {				\
	uint64_t __v;							\
									\
	if (quad_align && (((ptrdiff_t)((i) - (first))) & 1) == 1) {	\
		(i)++;							\
		(n)--;							\
	}								\
	if (quad_slots == 2)						\
		__v = (uint64_t)(uint32_t)(i)[0] |			\
		    ((uint64_t)(uint32_t)(i)[1]) << 32;			\
	else								\
		__v = (uint64_t)*(i);					\
	if (decimal)							\
		printf("%c%jd", (c), (intmax_t)__v);			\
	else								\
		printf("%c%#jx", (c), (uintmax_t)__v);			\
	(i) += quad_slots;						\
	(n) -= quad_slots;						\
	(c) = ',';							\
} while (0)

#define print_number(i,n,c) do {					\
	if (decimal)							\
		printf("%c%jd", c, (intmax_t)*i);			\
	else								\
		printf("%c%#jx", c, (uintmax_t)(u_register_t)*i);	\
	i++;								\
	n--;								\
	c = ',';							\
} while (0)

struct proc_info
{
	TAILQ_ENTRY(proc_info)	info;
	u_int			sv_flags;
	pid_t			pid;
};

static TAILQ_HEAD(trace_procs, proc_info) trace_procs;

#ifdef WITH_CASPER
static cap_channel_t *cappwd, *capgrp;

static int
cappwdgrp_setup(cap_channel_t **cappwdp, cap_channel_t **capgrpp)
{
	cap_channel_t *capcas, *cappwdloc, *capgrploc;
	const char *cmds[1], *fields[1];

	capcas = cap_init();
	if (capcas == NULL) {
		err(1, "unable to create casper process");
		exit(1);
	}
	cappwdloc = cap_service_open(capcas, "system.pwd");
	capgrploc = cap_service_open(capcas, "system.grp");
	/* Casper capability no longer needed. */
	cap_close(capcas);
	if (cappwdloc == NULL || capgrploc == NULL) {
		if (cappwdloc == NULL)
			warn("unable to open system.pwd service");
		if (capgrploc == NULL)
			warn("unable to open system.grp service");
		exit(1);
	}
	/* Limit system.pwd to only getpwuid() function and pw_name field. */
	cmds[0] = "getpwuid";
	if (cap_pwd_limit_cmds(cappwdloc, cmds, 1) < 0)
		err(1, "unable to limit system.pwd service");
	fields[0] = "pw_name";
	if (cap_pwd_limit_fields(cappwdloc, fields, 1) < 0)
		err(1, "unable to limit system.pwd service");
	/* Limit system.grp to only getgrgid() function and gr_name field. */
	cmds[0] = "getgrgid";
	if (cap_grp_limit_cmds(capgrploc, cmds, 1) < 0)
		err(1, "unable to limit system.grp service");
	fields[0] = "gr_name";
	if (cap_grp_limit_fields(capgrploc, fields, 1) < 0)
		err(1, "unable to limit system.grp service");

	*cappwdp = cappwdloc;
	*capgrpp = capgrploc;
	return (0);
}
#endif	/* WITH_CASPER */

static void
print_integer_arg(const char *(*decoder)(int), int value)
{
	const char *str;

	str = decoder(value);
	if (str != NULL)
		printf("%s", str);
	else {
		if (decimal)
			printf("<invalid=%d>", value);
		else
			printf("<invalid=%#x>", value);
	}
}

/* Like print_integer_arg but unknown values are treated as valid. */
static void
print_integer_arg_valid(const char *(*decoder)(int), int value)
{
	const char *str;

	str = decoder(value);
	if (str != NULL)
		printf("%s", str);
	else {
		if (decimal)
			printf("%d", value);
		else
			printf("%#x", value);
	}
}

static void
print_mask_arg(bool (*decoder)(FILE *, int, int *), int value)
{
	bool invalid;
	int rem;

	printf("%#x<", value);
	invalid = !decoder(stdout, value, &rem);
	printf(">");
	if (invalid)
		printf("<invalid>%u", rem);
}

static void
print_mask_arg0(bool (*decoder)(FILE *, int, int *), int value)
{
	bool invalid;
	int rem;

	if (value == 0) {
		printf("0");
		return;
	}
	printf("%#x<", value);
	invalid = !decoder(stdout, value, &rem);
	printf(">");
	if (invalid)
		printf("<invalid>%u", rem);
}

static void
decode_fileflags(fflags_t value)
{
	bool invalid;
	fflags_t rem;

	if (value == 0) {
		printf("0");
		return;
	}
	printf("%#x<", value);
	invalid = !sysdecode_fileflags(stdout, value, &rem);
	printf(">");
	if (invalid)
		printf("<invalid>%u", rem);
}

static void
decode_filemode(int value)
{
	bool invalid;
	int rem;

	if (value == 0) {
		printf("0");
		return;
	}
	printf("%#o<", value);
	invalid = !sysdecode_filemode(stdout, value, &rem);
	printf(">");
	if (invalid)
		printf("<invalid>%u", rem);
}

static void
print_mask_arg32(bool (*decoder)(FILE *, uint32_t, uint32_t *), uint32_t value)
{
	bool invalid;
	uint32_t rem;

	printf("%#x<", value);
	invalid = !decoder(stdout, value, &rem);
	printf(">");
	if (invalid)
		printf("<invalid>%u", rem);
}

static void
print_mask_argul(bool (*decoder)(FILE *, u_long, u_long *), u_long value)
{
	bool invalid;
	u_long rem;

	if (value == 0) {
		printf("0");
		return;
	}
	printf("%#lx<", value);
	invalid = !decoder(stdout, value, &rem);
	printf(">");
	if (invalid)
		printf("<invalid>%lu", rem);
}

int
main(int argc, char *argv[])
{
	int ch, ktrlen, size;
	void *m;
	int trpoints = ALL_POINTS;
	int drop_logged;
	pid_t pid = 0;
	u_int sv_flags;

	setlocale(LC_CTYPE, "");

	timestamp = TIMESTAMP_NONE;

	while ((ch = getopt(argc,argv,"f:dElm:np:AHRrSsTt:")) != -1)
		switch (ch) {
		case 'A':
			abiflag = true;
			break;
		case 'f':
			tracefile = optarg;
			break;
		case 'd':
			decimal = true;
			break;
		case 'l':
			tail = true;
			break;
		case 'm':
			maxdata = atoi(optarg);
			break;
		case 'n':
			fancy = false;
			break;
		case 'p':
			pid = atoi(optarg);
			break;
		case 'r':
			resolv = true;
			break;
		case 'S':
			syscallno = true;
			break;
		case 's':
			suppressdata = true;
			break;
		case 'E':
			timestamp |= TIMESTAMP_ELAPSED;
			break;
		case 'H':
			threads = true;
			break;
		case 'R':
			timestamp |= TIMESTAMP_RELATIVE;
			break;
		case 'T':
			timestamp |= TIMESTAMP_ABSOLUTE;
			break;
		case 't':
			trpoints = getpoints(optarg);
			if (trpoints < 0)
				errx(1, "unknown trace point in %s", optarg);
			break;
		default:
			usage();
		}

	if (argc > optind)
		usage();

	m = malloc(size = 1025);
	if (m == NULL)
		errx(1, "%s", strerror(ENOMEM));
	if (strcmp(tracefile, "-") != 0)
		if (!freopen(tracefile, "r", stdin))
			err(1, "%s", tracefile);

	caph_cache_catpages();
	caph_cache_tzdata();

#ifdef WITH_CASPER
	if (resolv) {
		if (cappwdgrp_setup(&cappwd, &capgrp) < 0) {
			cappwd = NULL;
			capgrp = NULL;
		}
	}
	if (!resolv || (cappwd != NULL && capgrp != NULL)) {
		if (caph_enter() < 0)
			err(1, "unable to enter capability mode");
	}
#else
	if (!resolv) {
		if (caph_enter() < 0)
			err(1, "unable to enter capability mode");
	}
#endif
	if (caph_limit_stdio() == -1)
		err(1, "unable to limit stdio");

	TAILQ_INIT(&trace_procs);
	drop_logged = 0;
	while (fread_tail(&ktr_header, sizeof(struct ktr_header), 1)) {
		if (ktr_header.ktr_type & KTR_DROP) {
			ktr_header.ktr_type &= ~KTR_DROP;
			if (!drop_logged && threads) {
				printf(
				    "%6jd %6jd %-8.*s Events dropped.\n",
				    (intmax_t)ktr_header.ktr_pid,
				    ktr_header.ktr_tid > 0 ?
				    (intmax_t)ktr_header.ktr_tid : 0,
				    MAXCOMLEN, ktr_header.ktr_comm);
				drop_logged = 1;
			} else if (!drop_logged) {
				printf("%6jd %-8.*s Events dropped.\n",
				    (intmax_t)ktr_header.ktr_pid, MAXCOMLEN,
				    ktr_header.ktr_comm);
				drop_logged = 1;
			}
		}
		if ((ktrlen = ktr_header.ktr_len) < 0)
			errx(1, "bogus length 0x%x", ktrlen);
		if (ktrlen > size) {
			m = realloc(m, ktrlen+1);
			if (m == NULL)
				errx(1, "%s", strerror(ENOMEM));
			size = ktrlen;
		}
		if (ktrlen && fread_tail(m, ktrlen, 1) == 0)
			errx(1, "data too short");
		if (fetchprocinfo(&ktr_header, (u_int *)m) != 0)
			continue;
		if (pid && ktr_header.ktr_pid != pid &&
		    ktr_header.ktr_tid != pid)
			continue;
		if ((trpoints & (1<<ktr_header.ktr_type)) == 0)
			continue;
		sv_flags = findabi(&ktr_header);
		dumpheader(&ktr_header, sv_flags);
		drop_logged = 0;
		switch (ktr_header.ktr_type) {
		case KTR_SYSCALL:
			ktrsyscall((struct ktr_syscall *)m, sv_flags);
			break;
		case KTR_SYSRET:
			ktrsysret((struct ktr_sysret *)m, sv_flags);
			break;
		case KTR_NAMEI:
		case KTR_SYSCTL:
			ktrnamei(m, ktrlen);
			break;
		case KTR_GENIO:
			ktrgenio((struct ktr_genio *)m, ktrlen);
			break;
		case KTR_PSIG:
			ktrpsig((struct ktr_psig *)m);
			break;
		case KTR_CSW:
			if (ktrlen == sizeof(struct ktr_csw_old))
				ktrcsw_old((struct ktr_csw_old *)m);
			else
				ktrcsw((struct ktr_csw *)m);
			break;
		case KTR_USER:
			ktruser(ktrlen, m);
			break;
		case KTR_STRUCT:
			ktrstruct(m, ktrlen);
			break;
		case KTR_CAPFAIL:
			ktrcapfail((struct ktr_cap_fail *)m);
			break;
		case KTR_FAULT:
			ktrfault((struct ktr_fault *)m);
			break;
		case KTR_FAULTEND:
			ktrfaultend((struct ktr_faultend *)m);
			break;
		case KTR_STRUCT_ARRAY:
			ktrstructarray((struct ktr_struct_array *)m, ktrlen);
			break;
		default:
			printf("\n");
			break;
		}
		if (tail)
			fflush(stdout);
	}
	return 0;
}

int
fread_tail(void *buf, int size, int num)
{
	int i;

	while ((i = fread(buf, size, num, stdin)) == 0 && tail) {
		sleep(1);
		clearerr(stdin);
	}
	return (i);
}

int
fetchprocinfo(struct ktr_header *kth, u_int *flags)
{
	struct proc_info *pi;

	switch (kth->ktr_type) {
	case KTR_PROCCTOR:
		TAILQ_FOREACH(pi, &trace_procs, info) {
			if (pi->pid == kth->ktr_pid) {
				TAILQ_REMOVE(&trace_procs, pi, info);
				break;
			}
		}
		pi = malloc(sizeof(struct proc_info));
		if (pi == NULL)
			errx(1, "%s", strerror(ENOMEM));
		pi->sv_flags = *flags;
		pi->pid = kth->ktr_pid;
		TAILQ_INSERT_TAIL(&trace_procs, pi, info);
		return (1);

	case KTR_PROCDTOR:
		TAILQ_FOREACH(pi, &trace_procs, info) {
			if (pi->pid == kth->ktr_pid) {
				TAILQ_REMOVE(&trace_procs, pi, info);
				free(pi);
				break;
			}
		}
		return (1);
	}

	return (0);
}

u_int
findabi(struct ktr_header *kth)
{
	struct proc_info *pi;

	TAILQ_FOREACH(pi, &trace_procs, info) {
		if (pi->pid == kth->ktr_pid) {
			return (pi->sv_flags);
		}
	}
	return (0);
}

void
dumpheader(struct ktr_header *kth, u_int sv_flags)
{
	static char unknown[64];
	static struct timeval prevtime, prevtime_e;
	struct timeval temp;
	const char *abi;
	const char *arch;
	const char *type;
	const char *sign;

	switch (kth->ktr_type) {
	case KTR_SYSCALL:
		type = "CALL";
		break;
	case KTR_SYSRET:
		type = "RET ";
		break;
	case KTR_NAMEI:
		type = "NAMI";
		break;
	case KTR_GENIO:
		type = "GIO ";
		break;
	case KTR_PSIG:
		type = "PSIG";
		break;
	case KTR_CSW:
		type = "CSW ";
		break;
	case KTR_USER:
		type = "USER";
		break;
	case KTR_STRUCT:
	case KTR_STRUCT_ARRAY:
		type = "STRU";
		break;
	case KTR_SYSCTL:
		type = "SCTL";
		break;
	case KTR_CAPFAIL:
		type = "CAP ";
		break;
	case KTR_FAULT:
		type = "PFLT";
		break;
	case KTR_FAULTEND:
		type = "PRET";
		break;
	default:
		sprintf(unknown, "UNKNOWN(%d)", kth->ktr_type);
		type = unknown;
	}

	/*
	 * The ktr_tid field was previously the ktr_buffer field, which held
	 * the kernel pointer value for the buffer associated with data
	 * following the record header.  It now holds a threadid, but only
	 * for trace files after the change.  Older trace files still contain
	 * kernel pointers.  Detect this and suppress the results by printing
	 * negative tid's as 0.
	 */
	if (threads)
		printf("%6jd %6jd %-8.*s ", (intmax_t)kth->ktr_pid,
		    kth->ktr_tid > 0 ? (intmax_t)kth->ktr_tid : 0,
		    MAXCOMLEN, kth->ktr_comm);
	else
		printf("%6jd %-8.*s ", (intmax_t)kth->ktr_pid, MAXCOMLEN,
		    kth->ktr_comm);
        if (timestamp) {
		if (timestamp & TIMESTAMP_ABSOLUTE) {
			printf("%jd.%06ld ", (intmax_t)kth->ktr_time.tv_sec,
			    kth->ktr_time.tv_usec);
		}
		if (timestamp & TIMESTAMP_ELAPSED) {
			if (prevtime_e.tv_sec == 0)
				prevtime_e = kth->ktr_time;
			timersub(&kth->ktr_time, &prevtime_e, &temp);
			printf("%jd.%06ld ", (intmax_t)temp.tv_sec,
			    temp.tv_usec);
		}
		if (timestamp & TIMESTAMP_RELATIVE) {
			if (prevtime.tv_sec == 0)
				prevtime = kth->ktr_time;
			if (timercmp(&kth->ktr_time, &prevtime, <)) {
				timersub(&prevtime, &kth->ktr_time, &temp);
				sign = "-";
			} else {
				timersub(&kth->ktr_time, &prevtime, &temp);
				sign = "";
			}
			prevtime = kth->ktr_time;
			printf("%s%jd.%06ld ", sign, (intmax_t)temp.tv_sec,
			    temp.tv_usec);
		}
	}
	printf("%s  ", type);
	if (abiflag != 0) {
		switch (sv_flags & SV_ABI_MASK) {
		case SV_ABI_LINUX:
			abi = "L";
			break;
		case SV_ABI_FREEBSD:
			abi = "F";
			break;
		case SV_ABI_CLOUDABI:
			abi = "C";
			break;
		default:
			abi = "U";
			break;
		}

		if ((sv_flags & SV_LP64) != 0)
			arch = "64";
		else if ((sv_flags & SV_ILP32) != 0)
			arch = "32";
		else
			arch = "00";

		printf("%s%s  ", abi, arch);
	}
}

#include <sys/syscall.h>

static void
ioctlname(unsigned long val)
{
	const char *str;

	str = sysdecode_ioctlname(val);
	if (str != NULL)
		printf("%s", str);
	else if (decimal)
		printf("%lu", val);
	else
		printf("%#lx", val);
}

static enum sysdecode_abi
syscallabi(u_int sv_flags)
{

	if (sv_flags == 0)
		return (SYSDECODE_ABI_FREEBSD);
	switch (sv_flags & SV_ABI_MASK) {
	case SV_ABI_FREEBSD:
		return (SYSDECODE_ABI_FREEBSD);
	case SV_ABI_LINUX:
#ifdef __LP64__
		if (sv_flags & SV_ILP32)
			return (SYSDECODE_ABI_LINUX32);
#endif
		return (SYSDECODE_ABI_LINUX);
	case SV_ABI_CLOUDABI:
		return (SYSDECODE_ABI_CLOUDABI64);
	default:
		return (SYSDECODE_ABI_UNKNOWN);
	}
}

static void
syscallname(u_int code, u_int sv_flags)
{
	const char *name;

	name = sysdecode_syscallname(syscallabi(sv_flags), code);
	if (name == NULL)
		printf("[%d]", code);
	else {
		printf("%s", name);
		if (syscallno)
			printf("[%d]", code);
	}
}

static void
print_signal(int signo)
{
	const char *signame;

	signame = sysdecode_signal(signo);
	if (signame != NULL)
		printf("%s", signame);
	else
		printf("SIG %d", signo);
}

void
ktrsyscall(struct ktr_syscall *ktr, u_int sv_flags)
{
	int narg = ktr->ktr_narg;
	register_t *ip, *first;
	intmax_t arg;
	int quad_align, quad_slots;

	syscallname(ktr->ktr_code, sv_flags);
	ip = first = &ktr->ktr_args[0];
	if (narg) {
		char c = '(';
		if (fancy &&
		    (sv_flags == 0 ||
		    (sv_flags & SV_ABI_MASK) == SV_ABI_FREEBSD)) {
			quad_align = 0;
			if (sv_flags & SV_ILP32) {
#ifdef __powerpc__
				quad_align = 1;
#endif
				quad_slots = 2;
			} else
				quad_slots = 1;
			switch (ktr->ktr_code) {
			case SYS_bindat:
			case SYS_chflagsat:
			case SYS_connectat:
			case SYS_faccessat:
			case SYS_fchmodat:
			case SYS_fchownat:
			case SYS_fstatat:
			case SYS_futimesat:
			case SYS_linkat:
			case SYS_mkdirat:
			case SYS_mkfifoat:
			case SYS_mknodat:
			case SYS_openat:
			case SYS_readlinkat:
			case SYS_renameat:
			case SYS_unlinkat:
			case SYS_utimensat:
				putchar('(');
				print_integer_arg_valid(sysdecode_atfd, *ip);
				c = ',';
				ip++;
				narg--;
				break;
			}
			switch (ktr->ktr_code) {
			case SYS_ioctl: {
				print_number(ip, narg, c);
				putchar(c);
				ioctlname(*ip);
				c = ',';
				ip++;
				narg--;
				break;
			}
			case SYS_ptrace:
				putchar('(');
				print_integer_arg(sysdecode_ptrace_request, *ip);
				c = ',';
				ip++;
				narg--;
				break;
			case SYS_access:
			case SYS_eaccess:
			case SYS_faccessat:
				print_number(ip, narg, c);
				putchar(',');
				print_mask_arg(sysdecode_access_mode, *ip);
				ip++;
				narg--;
				break;
			case SYS_open:
			case SYS_openat:
				print_number(ip, narg, c);
				putchar(',');
				print_mask_arg(sysdecode_open_flags, ip[0]);
				if ((ip[0] & O_CREAT) == O_CREAT) {
					putchar(',');
					decode_filemode(ip[1]);
				}
				ip += 2;
				narg -= 2;
				break;
			case SYS_wait4:
				print_number(ip, narg, c);
				print_number(ip, narg, c);
				putchar(',');
				print_mask_arg0(sysdecode_wait4_options, *ip);
				ip++;
				narg--;
				break;
			case SYS_wait6:
				putchar('(');
				print_integer_arg(sysdecode_idtype, *ip);
				c = ',';
				ip++;
				narg--;
				print_number64(first, ip, narg, c);
				print_number(ip, narg, c);
				putchar(',');
				print_mask_arg(sysdecode_wait6_options, *ip);
				ip++;
				narg--;
				break;
			case SYS_chmod:
			case SYS_fchmod:
			case SYS_lchmod:
			case SYS_fchmodat:
				print_number(ip, narg, c);
				putchar(',');
				decode_filemode(*ip);
				ip++;
				narg--;
				break;
			case SYS_mknodat:
				print_number(ip, narg, c);
				putchar(',');
				decode_filemode(*ip);
				ip++;
				narg--;
				break;
			case SYS_getfsstat:
				print_number(ip, narg, c);
				print_number(ip, narg, c);
				putchar(',');
				print_integer_arg(sysdecode_getfsstat_mode, *ip);
				ip++;
				narg--;
				break;
			case SYS_mount:
				print_number(ip, narg, c);
				print_number(ip, narg, c);
				putchar(',');
				print_mask_arg(sysdecode_mount_flags, *ip);
				ip++;
				narg--;
				break;
			case SYS_unmount:
				print_number(ip, narg, c);
				putchar(',');
				print_mask_arg(sysdecode_mount_flags, *ip);
				ip++;
				narg--;
				break;
			case SYS_recvmsg:
			case SYS_sendmsg:
				print_number(ip, narg, c);
				print_number(ip, narg, c);
				putchar(',');
				print_mask_arg0(sysdecode_msg_flags, *ip);
				ip++;
				narg--;
				break;
			case SYS_recvfrom:
			case SYS_sendto:
				print_number(ip, narg, c);
				print_number(ip, narg, c);
				print_number(ip, narg, c);
				putchar(',');
				print_mask_arg0(sysdecode_msg_flags, *ip);
				ip++;
				narg--;
				break;
			case SYS_chflags:
			case SYS_chflagsat:
			case SYS_fchflags:
			case SYS_lchflags:
				print_number(ip, narg, c);
				putchar(',');
				decode_fileflags(*ip);
				ip++;
				narg--;
				break;
			case SYS_kill:
				print_number(ip, narg, c);
				putchar(',');
				print_signal(*ip);
				ip++;
				narg--;
				break;
			case SYS_reboot:
				putchar('(');
				print_mask_arg(sysdecode_reboot_howto, *ip);
				ip++;
				narg--;
				break;
			case SYS_umask:
				putchar('(');
				decode_filemode(*ip);
				ip++;
				narg--;
				break;
			case SYS_msync:
				print_number(ip, narg, c);
				print_number(ip, narg, c);
				putchar(',');
				print_mask_arg(sysdecode_msync_flags, *ip);
				ip++;
				narg--;
				break;
#ifdef SYS_freebsd6_mmap
			case SYS_freebsd6_mmap:
				print_number(ip, narg, c);
				print_number(ip, narg, c);
				putchar(',');
				print_mask_arg(sysdecode_mmap_prot, *ip);
				putchar(',');
				ip++;
				narg--;
				print_mask_arg(sysdecode_mmap_flags, *ip);
				ip++;
				narg--;
				break;
#endif
			case SYS_mmap:
				print_number(ip, narg, c);
				print_number(ip, narg, c);
				putchar(',');
				print_mask_arg(sysdecode_mmap_prot, *ip);
				putchar(',');
				ip++;
				narg--;
				print_mask_arg(sysdecode_mmap_flags, *ip);
				ip++;
				narg--;
				break;
			case SYS_mprotect:
				print_number(ip, narg, c);
				print_number(ip, narg, c);
				putchar(',');
				print_mask_arg(sysdecode_mmap_prot, *ip);
				ip++;
				narg--;
				break;
			case SYS_madvise:
				print_number(ip, narg, c);
				print_number(ip, narg, c);
				putchar(',');
				print_integer_arg(sysdecode_madvice, *ip);
				ip++;
				narg--;
				break;
			case SYS_pathconf:
			case SYS_lpathconf:
			case SYS_fpathconf:
				print_number(ip, narg, c);
				putchar(',');
				print_integer_arg(sysdecode_pathconf_name, *ip);
				ip++;
				narg--;
				break;
			case SYS_getpriority:
			case SYS_setpriority:
				putchar('(');
				print_integer_arg(sysdecode_prio_which, *ip);
				c = ',';
				ip++;
				narg--;
				break;
			case SYS_fcntl:
				print_number(ip, narg, c);
				putchar(',');
				print_integer_arg(sysdecode_fcntl_cmd, ip[0]);
				if (sysdecode_fcntl_arg_p(ip[0])) {
					putchar(',');
					if (ip[0] == F_SETFL)
						print_mask_arg(
						    sysdecode_fcntl_fileflags,
							ip[1]);
					else
						sysdecode_fcntl_arg(stdout,
						    ip[0], ip[1],
						    decimal ? 10 : 16);
				}
				ip += 2;
				narg -= 2;
				break;
			case SYS_socket: {
				int sockdomain;
				putchar('(');
				sockdomain = *ip;
				print_integer_arg(sysdecode_socketdomain,
				    sockdomain);
				ip++;
				narg--;
				putchar(',');
				print_mask_arg(sysdecode_socket_type, *ip);
				ip++;
				narg--;
				if (sockdomain == PF_INET ||
				    sockdomain == PF_INET6) {
					putchar(',');
					print_integer_arg(sysdecode_ipproto,
					    *ip);
					ip++;
					narg--;
				}
				c = ',';
				break;
			}
			case SYS_setsockopt:
			case SYS_getsockopt: {
				const char *str;

				print_number(ip, narg, c);
				putchar(',');
				print_integer_arg_valid(sysdecode_sockopt_level,
				    *ip);
				str = sysdecode_sockopt_name(ip[0], ip[1]);
				if (str != NULL) {
					printf(",%s", str);
					ip++;
					narg--;
				}
				ip++;
				narg--;
				break;
			}
#ifdef SYS_freebsd6_lseek
			case SYS_freebsd6_lseek:
				print_number(ip, narg, c);
				/* Hidden 'pad' argument, not in lseek(2) */
				print_number(ip, narg, c);
				print_number64(first, ip, narg, c);
				putchar(',');
				print_integer_arg(sysdecode_whence, *ip);
				ip++;
				narg--;
				break;
#endif
			case SYS_lseek:
				print_number(ip, narg, c);
				print_number64(first, ip, narg, c);
				putchar(',');
				print_integer_arg(sysdecode_whence, *ip);
				ip++;
				narg--;
				break;
			case SYS_flock:
				print_number(ip, narg, c);
				putchar(',');
				print_mask_arg(sysdecode_flock_operation, *ip);
				ip++;
				narg--;
				break;
			case SYS_mkfifo:
			case SYS_mkfifoat:
			case SYS_mkdir:
			case SYS_mkdirat:
				print_number(ip, narg, c);
				putchar(',');
				decode_filemode(*ip);
				ip++;
				narg--;
				break;
			case SYS_shutdown:
				print_number(ip, narg, c);
				putchar(',');
				print_integer_arg(sysdecode_shutdown_how, *ip);
				ip++;
				narg--;
				break;
			case SYS_socketpair:
				putchar('(');
				print_integer_arg(sysdecode_socketdomain, *ip);
				ip++;
				narg--;
				putchar(',');
				print_mask_arg(sysdecode_socket_type, *ip);
				ip++;
				narg--;
				c = ',';
				break;
			case SYS_getrlimit:
			case SYS_setrlimit:
				putchar('(');
				print_integer_arg(sysdecode_rlimit, *ip);
				ip++;
				narg--;
				c = ',';
				break;
			case SYS_getrusage:
				putchar('(');
				print_integer_arg(sysdecode_getrusage_who, *ip);
				ip++;
				narg--;
				c = ',';
				break;
			case SYS_quotactl:
				print_number(ip, narg, c);
				putchar(',');
				if (!sysdecode_quotactl_cmd(stdout, *ip)) {
					if (decimal)
						printf("<invalid=%d>", (int)*ip);
					else
						printf("<invalid=%#x>",
						    (int)*ip);
				}
				ip++;
				narg--;
				c = ',';
				break;
			case SYS_nfssvc:
				putchar('(');
				print_integer_arg(sysdecode_nfssvc_flags, *ip);
				ip++;
				narg--;
				c = ',';
				break;
			case SYS_rtprio:
			case SYS_rtprio_thread:
				putchar('(');
				print_integer_arg(sysdecode_rtprio_function,
				    *ip);
				ip++;
				narg--;
				c = ',';
				break;
			case SYS___semctl:
				print_number(ip, narg, c);
				print_number(ip, narg, c);
				putchar(',');
				print_integer_arg(sysdecode_semctl_cmd, *ip);
				ip++;
				narg--;
				break;
			case SYS_semget:
				print_number(ip, narg, c);
				print_number(ip, narg, c);
				putchar(',');
				print_mask_arg(sysdecode_semget_flags, *ip);
				ip++;
				narg--;
				break;
			case SYS_msgctl:
				print_number(ip, narg, c);
				putchar(',');
				print_integer_arg(sysdecode_msgctl_cmd, *ip);
				ip++;
				narg--;
				break;
			case SYS_shmat:
				print_number(ip, narg, c);
				print_number(ip, narg, c);
				putchar(',');
				print_mask_arg(sysdecode_shmat_flags, *ip);
				ip++;
				narg--;
				break;
			case SYS_shmctl:
				print_number(ip, narg, c);
				putchar(',');
				print_integer_arg(sysdecode_shmctl_cmd, *ip);
				ip++;
				narg--;
				break;
			case SYS_shm_open:
				print_number(ip, narg, c);
				putchar(',');
				print_mask_arg(sysdecode_open_flags, ip[0]);
				putchar(',');
				decode_filemode(ip[1]);
				ip += 2;
				narg -= 2;
				break;
			case SYS_minherit:
				print_number(ip, narg, c);
				print_number(ip, narg, c);
				putchar(',');
				print_integer_arg(sysdecode_minherit_inherit,
				    *ip);
				ip++;
				narg--;
				break;
			case SYS_rfork:
				putchar('(');
				print_mask_arg(sysdecode_rfork_flags, *ip);
				ip++;
				narg--;
				c = ',';
				break;
			case SYS_lio_listio:
				putchar('(');
				print_integer_arg(sysdecode_lio_listio_mode,
				    *ip);
				ip++;
				narg--;
				c = ',';
				break;
			case SYS_mlockall:
				putchar('(');
				print_mask_arg(sysdecode_mlockall_flags, *ip);
				ip++;
				narg--;
				break;
			case SYS_sched_setscheduler:
				print_number(ip, narg, c);
				putchar(',');
				print_integer_arg(sysdecode_scheduler_policy,
				    *ip);
				ip++;
				narg--;
				break;
			case SYS_sched_get_priority_max:
			case SYS_sched_get_priority_min:
				putchar('(');
				print_integer_arg(sysdecode_scheduler_policy,
				    *ip);
				ip++;
				narg--;
				break;
			case SYS_sendfile:
				print_number(ip, narg, c);
				print_number(ip, narg, c);
				print_number(ip, narg, c);
				print_number(ip, narg, c);
				print_number(ip, narg, c);
				print_number(ip, narg, c);
				putchar(',');
				print_mask_arg(sysdecode_sendfile_flags, *ip);
				ip++;
				narg--;
				break;
			case SYS_kldsym:
				print_number(ip, narg, c);
				putchar(',');
				print_integer_arg(sysdecode_kldsym_cmd, *ip);
				ip++;
				narg--;
				break;
			case SYS_sigprocmask:
				putchar('(');
				print_integer_arg(sysdecode_sigprocmask_how,
				    *ip);
				ip++;
				narg--;
				c = ',';
				break;
			case SYS___acl_get_file:
			case SYS___acl_set_file:
			case SYS___acl_get_fd:
			case SYS___acl_set_fd:
			case SYS___acl_delete_file:
			case SYS___acl_delete_fd:
			case SYS___acl_aclcheck_file:
			case SYS___acl_aclcheck_fd:
			case SYS___acl_get_link:
			case SYS___acl_set_link:
			case SYS___acl_delete_link:
			case SYS___acl_aclcheck_link:
				print_number(ip, narg, c);
				putchar(',');
				print_integer_arg(sysdecode_acltype, *ip);
				ip++;
				narg--;
				break;
			case SYS_sigaction:
				putchar('(');
				print_signal(*ip);
				ip++;
				narg--;
				c = ',';
				break;
			case SYS_extattrctl:
				print_number(ip, narg, c);
				putchar(',');
				print_integer_arg(sysdecode_extattrnamespace,
				    *ip);
				ip++;
				narg--;
				break;
			case SYS_nmount:
				print_number(ip, narg, c);
				print_number(ip, narg, c);
				putchar(',');
				print_mask_arg(sysdecode_mount_flags, *ip);
				ip++;
				narg--;
				break;
			case SYS_thr_create:
				print_number(ip, narg, c);
				print_number(ip, narg, c);
				putchar(',');
				print_mask_arg(sysdecode_thr_create_flags, *ip);
				ip++;
				narg--;
				break;
			case SYS_thr_kill:
				print_number(ip, narg, c);
				putchar(',');
				print_signal(*ip);
				ip++;
				narg--;
				break;
			case SYS_kldunloadf:
				print_number(ip, narg, c);
				putchar(',');
				print_integer_arg(sysdecode_kldunload_flags,
				    *ip);
				ip++;
				narg--;
				break;
			case SYS_linkat:
			case SYS_renameat:
			case SYS_symlinkat:
				print_number(ip, narg, c);
				putchar(',');
				print_integer_arg_valid(sysdecode_atfd, *ip);
				ip++;
				narg--;
				print_number(ip, narg, c);
				break;
			case SYS_cap_fcntls_limit:
				print_number(ip, narg, c);
				putchar(',');
				arg = *ip;
				ip++;
				narg--;
				print_mask_arg32(sysdecode_cap_fcntlrights, arg);
				break;
			case SYS_posix_fadvise:
				print_number(ip, narg, c);
				print_number(ip, narg, c);
				print_number(ip, narg, c);
				(void)putchar(',');
				print_integer_arg(sysdecode_fadvice, *ip);
				ip++;
				narg--;
				break;
			case SYS_procctl:
				putchar('(');
				print_integer_arg(sysdecode_idtype, *ip);
				c = ',';
				ip++;
				narg--;
				print_number64(first, ip, narg, c);
				putchar(',');
				print_integer_arg(sysdecode_procctl_cmd, *ip);
				ip++;
				narg--;
				break;
			case SYS__umtx_op:
				print_number(ip, narg, c);
				putchar(',');
				print_integer_arg(sysdecode_umtx_op, *ip);
				switch (*ip) {
				case UMTX_OP_CV_WAIT:
					ip++;
					narg--;
					putchar(',');
					print_mask_argul(
					    sysdecode_umtx_cvwait_flags, *ip);
					break;
				case UMTX_OP_RW_RDLOCK:
					ip++;
					narg--;
					putchar(',');
					print_mask_argul(
					    sysdecode_umtx_rwlock_flags, *ip);
					break;
				}
				ip++;
				narg--;
				break;
			case SYS_ftruncate:
			case SYS_truncate:
				print_number(ip, narg, c);
				print_number64(first, ip, narg, c);
				break;
			case SYS_fchownat:
				print_number(ip, narg, c);
				print_number(ip, narg, c);
				print_number(ip, narg, c);
				break;
			case SYS_fstatat:
			case SYS_utimensat:
				print_number(ip, narg, c);
				print_number(ip, narg, c);
				break;
			case SYS_unlinkat:
				print_number(ip, narg, c);
				break;
			case SYS_sysarch:
				putchar('(');
				print_integer_arg(sysdecode_sysarch_number, *ip);
				ip++;
				narg--;
				c = ',';
				break;
			}
			switch (ktr->ktr_code) {
			case SYS_chflagsat:
			case SYS_fchownat:
			case SYS_faccessat:
			case SYS_fchmodat:
			case SYS_fstatat:
			case SYS_linkat:
			case SYS_unlinkat:
			case SYS_utimensat:
				putchar(',');
				print_mask_arg0(sysdecode_atflags, *ip);
				ip++;
				narg--;
				break;
			}
		}
		while (narg > 0) {
			print_number(ip, narg, c);
		}
		putchar(')');
	}
	putchar('\n');
}

void
ktrsysret(struct ktr_sysret *ktr, u_int sv_flags)
{
	register_t ret = ktr->ktr_retval;
	int error = ktr->ktr_error;

	syscallname(ktr->ktr_code, sv_flags);
	printf(" ");

	if (error == 0) {
		if (fancy) {
			printf("%ld", (long)ret);
			if (ret < 0 || ret > 9)
				printf("/%#lx", (unsigned long)ret);
		} else {
			if (decimal)
				printf("%ld", (long)ret);
			else
				printf("%#lx", (unsigned long)ret);
		}
	} else if (error == ERESTART)
		printf("RESTART");
	else if (error == EJUSTRETURN)
		printf("JUSTRETURN");
	else {
		printf("-1 errno %d", sysdecode_freebsd_to_abi_errno(
		    syscallabi(sv_flags), error));
		if (fancy)
			printf(" %s", strerror(ktr->ktr_error));
	}
	putchar('\n');
}

void
ktrnamei(char *cp, int len)
{
	printf("\"%.*s\"\n", len, cp);
}

void
hexdump(char *p, int len, int screenwidth)
{
	int n, i;
	int width;

	width = 0;
	do {
		width += 2;
		i = 13;			/* base offset */
		i += (width / 2) + 1;	/* spaces every second byte */
		i += (width * 2);	/* width of bytes */
		i += 3;			/* "  |" */
		i += width;		/* each byte */
		i += 1;			/* "|" */
	} while (i < screenwidth);
	width -= 2;

	for (n = 0; n < len; n += width) {
		for (i = n; i < n + width; i++) {
			if ((i % width) == 0) {	/* beginning of line */
				printf("       0x%04x", i);
			}
			if ((i % 2) == 0) {
				printf(" ");
			}
			if (i < len)
				printf("%02x", p[i] & 0xff);
			else
				printf("  ");
		}
		printf("  |");
		for (i = n; i < n + width; i++) {
			if (i >= len)
				break;
			if (p[i] >= ' ' && p[i] <= '~')
				printf("%c", p[i]);
			else
				printf(".");
		}
		printf("|\n");
	}
	if ((i % width) != 0)
		printf("\n");
}

void
visdump(char *dp, int datalen, int screenwidth)
{
	int col = 0;
	char *cp;
	int width;
	char visbuf[5];

	printf("       \"");
	col = 8;
	for (;datalen > 0; datalen--, dp++) {
		 vis(visbuf, *dp, VIS_CSTYLE, *(dp+1));
		cp = visbuf;
		/*
		 * Keep track of printables and
		 * space chars (like fold(1)).
		 */
		if (col == 0) {
			putchar('\t');
			col = 8;
		}
		switch(*cp) {
		case '\n':
			col = 0;
			putchar('\n');
			continue;
		case '\t':
			width = 8 - (col&07);
			break;
		default:
			width = strlen(cp);
		}
		if (col + width > (screenwidth-2)) {
			printf("\\\n\t");
			col = 8;
		}
		col += width;
		do {
			putchar(*cp++);
		} while (*cp);
	}
	if (col == 0)
		printf("       ");
	printf("\"\n");
}

void
ktrgenio(struct ktr_genio *ktr, int len)
{
	int datalen = len - sizeof (struct ktr_genio);
	char *dp = (char *)ktr + sizeof (struct ktr_genio);
	static int screenwidth = 0;
	int i, binary;

	printf("fd %d %s %d byte%s\n", ktr->ktr_fd,
		ktr->ktr_rw == UIO_READ ? "read" : "wrote", datalen,
		datalen == 1 ? "" : "s");
	if (suppressdata)
		return;
	if (screenwidth == 0) {
		struct winsize ws;

		if (fancy && ioctl(fileno(stderr), TIOCGWINSZ, &ws) != -1 &&
		    ws.ws_col > 8)
			screenwidth = ws.ws_col;
		else
			screenwidth = 80;
	}
	if (maxdata && datalen > maxdata)
		datalen = maxdata;

	for (i = 0, binary = 0; i < datalen && binary == 0; i++)  {
		if (dp[i] >= 32 && dp[i] < 127)
			continue;
		if (dp[i] == 10 || dp[i] == 13 || dp[i] == 0 || dp[i] == 9)
			continue;
		binary = 1;
	}
	if (binary)
		hexdump(dp, datalen, screenwidth);
	else
		visdump(dp, datalen, screenwidth);
}

void
ktrpsig(struct ktr_psig *psig)
{
	const char *str;

	print_signal(psig->signo);
	if (psig->action == SIG_DFL) {
		printf(" SIG_DFL");
	} else {
		printf(" caught handler=0x%lx mask=0x%x",
		    (u_long)psig->action, psig->mask.__bits[0]);
	}
	printf(" code=");
	str = sysdecode_sigcode(psig->signo, psig->code);
	if (str != NULL)
		printf("%s", str);
	else
		printf("<invalid=%#x>", psig->code);
	putchar('\n');
}

void
ktrcsw_old(struct ktr_csw_old *cs)
{
	printf("%s %s\n", cs->out ? "stop" : "resume",
		cs->user ? "user" : "kernel");
}

void
ktrcsw(struct ktr_csw *cs)
{
	printf("%s %s \"%s\"\n", cs->out ? "stop" : "resume",
	    cs->user ? "user" : "kernel", cs->wmesg);
}

void
ktruser(int len, void *p)
{
	unsigned char *cp;

	if (sysdecode_utrace(stdout, p, len)) {
		printf("\n");
		return;
	}

	printf("%d ", len);
	cp = p;
	while (len--)
		if (decimal)
			printf(" %d", *cp++);
		else
			printf(" %02x", *cp++);
	printf("\n");
}

void
ktrcaprights(cap_rights_t *rightsp)
{

	printf("cap_rights_t ");
	sysdecode_cap_rights(stdout, rightsp);
	printf("\n");
}

static void
ktrtimeval(struct timeval *tv)
{

	printf("{%ld, %ld}", (long)tv->tv_sec, tv->tv_usec);
}

void
ktritimerval(struct itimerval *it)
{

	printf("itimerval { .interval = ");
	ktrtimeval(&it->it_interval);
	printf(", .value = ");
	ktrtimeval(&it->it_value);
	printf(" }\n");
}

void
ktrsockaddr(struct sockaddr *sa)
{
/*
 TODO: Support additional address families
	#include <netsmb/netbios.h>
	struct sockaddr_nb	*nb;
*/
	const char *str;
	char addr[64];

	/*
	 * note: ktrstruct() has already verified that sa points to a
	 * buffer at least sizeof(struct sockaddr) bytes long and exactly
	 * sa->sa_len bytes long.
	 */
	printf("struct sockaddr { ");
	str = sysdecode_sockaddr_family(sa->sa_family);
	if (str != NULL)
		printf("%s", str);
	else
		printf("<invalid=%d>", sa->sa_family);
	printf(", ");

#define check_sockaddr_len(n)					\
	if (sa_##n.s##n##_len < sizeof(struct sockaddr_##n)) {	\
		printf("invalid");				\
		break;						\
	}

	switch(sa->sa_family) {
	case AF_INET: {
		struct sockaddr_in sa_in;

		memset(&sa_in, 0, sizeof(sa_in));
		memcpy(&sa_in, sa, sa->sa_len);
		check_sockaddr_len(in);
		inet_ntop(AF_INET, &sa_in.sin_addr, addr, sizeof addr);
		printf("%s:%u", addr, ntohs(sa_in.sin_port));
		break;
	}
	case AF_INET6: {
		struct sockaddr_in6 sa_in6;

		memset(&sa_in6, 0, sizeof(sa_in6));
		memcpy(&sa_in6, sa, sa->sa_len);
		check_sockaddr_len(in6);
		getnameinfo((struct sockaddr *)&sa_in6, sizeof(sa_in6),
		    addr, sizeof(addr), NULL, 0, NI_NUMERICHOST);
		printf("[%s]:%u", addr, htons(sa_in6.sin6_port));
		break;
	}
	case AF_UNIX: {
		struct sockaddr_un sa_un;

		memset(&sa_un, 0, sizeof(sa_un));
		memcpy(&sa_un, sa, sa->sa_len);
		printf("%.*s", (int)sizeof(sa_un.sun_path), sa_un.sun_path);
		break;
	}
	default:
		printf("unknown address family");
	}
	printf(" }\n");
}

void
ktrstat(struct stat *statp)
{
	char mode[12], timestr[PATH_MAX + 4];
	struct passwd *pwd;
	struct group  *grp;
	struct tm *tm;

	/*
	 * note: ktrstruct() has already verified that statp points to a
	 * buffer exactly sizeof(struct stat) bytes long.
	 */
	printf("struct stat {");
	printf("dev=%ju, ino=%ju, ",
		(uintmax_t)statp->st_dev, (uintmax_t)statp->st_ino);
	if (!resolv)
		printf("mode=0%jo, ", (uintmax_t)statp->st_mode);
	else {
		strmode(statp->st_mode, mode);
		printf("mode=%s, ", mode);
	}
	printf("nlink=%ju, ", (uintmax_t)statp->st_nlink);
	if (!resolv) {
		pwd = NULL;
	} else {
#ifdef WITH_CASPER
		if (cappwd != NULL)
			pwd = cap_getpwuid(cappwd, statp->st_uid);
		else
#endif
			pwd = getpwuid(statp->st_uid);
	}
	if (pwd == NULL)
		printf("uid=%ju, ", (uintmax_t)statp->st_uid);
	else
		printf("uid=\"%s\", ", pwd->pw_name);
	if (!resolv) {
		grp = NULL;
	} else {
#ifdef WITH_CASPER
		if (capgrp != NULL)
			grp = cap_getgrgid(capgrp, statp->st_gid);
		else
#endif
			grp = getgrgid(statp->st_gid);
	}
	if (grp == NULL)
		printf("gid=%ju, ", (uintmax_t)statp->st_gid);
	else
		printf("gid=\"%s\", ", grp->gr_name);
	printf("rdev=%ju, ", (uintmax_t)statp->st_rdev);
	printf("atime=");
	if (!resolv)
		printf("%jd", (intmax_t)statp->st_atim.tv_sec);
	else {
		tm = localtime(&statp->st_atim.tv_sec);
		strftime(timestr, sizeof(timestr), TIME_FORMAT, tm);
		printf("\"%s\"", timestr);
	}
	if (statp->st_atim.tv_nsec != 0)
		printf(".%09ld, ", statp->st_atim.tv_nsec);
	else
		printf(", ");
	printf("mtime=");
	if (!resolv)
		printf("%jd", (intmax_t)statp->st_mtim.tv_sec);
	else {
		tm = localtime(&statp->st_mtim.tv_sec);
		strftime(timestr, sizeof(timestr), TIME_FORMAT, tm);
		printf("\"%s\"", timestr);
	}
	if (statp->st_mtim.tv_nsec != 0)
		printf(".%09ld, ", statp->st_mtim.tv_nsec);
	else
		printf(", ");
	printf("ctime=");
	if (!resolv)
		printf("%jd", (intmax_t)statp->st_ctim.tv_sec);
	else {
		tm = localtime(&statp->st_ctim.tv_sec);
		strftime(timestr, sizeof(timestr), TIME_FORMAT, tm);
		printf("\"%s\"", timestr);
	}
	if (statp->st_ctim.tv_nsec != 0)
		printf(".%09ld, ", statp->st_ctim.tv_nsec);
	else
		printf(", ");
	printf("birthtime=");
	if (!resolv)
		printf("%jd", (intmax_t)statp->st_birthtim.tv_sec);
	else {
		tm = localtime(&statp->st_birthtim.tv_sec);
		strftime(timestr, sizeof(timestr), TIME_FORMAT, tm);
		printf("\"%s\"", timestr);
	}
	if (statp->st_birthtim.tv_nsec != 0)
		printf(".%09ld, ", statp->st_birthtim.tv_nsec);
	else
		printf(", ");
	printf("size=%jd, blksize=%ju, blocks=%jd, flags=0x%x",
		(uintmax_t)statp->st_size, (uintmax_t)statp->st_blksize,
		(intmax_t)statp->st_blocks, statp->st_flags);
	printf(" }\n");
}

void
ktrstruct(char *buf, size_t buflen)
{
	char *name, *data;
	size_t namelen, datalen;
	int i;
	cap_rights_t rights;
	struct itimerval it;
	struct stat sb;
	struct sockaddr_storage ss;

	for (name = buf, namelen = 0;
	     namelen < buflen && name[namelen] != '\0';
	     ++namelen)
		/* nothing */;
	if (namelen == buflen)
		goto invalid;
	if (name[namelen] != '\0')
		goto invalid;
	data = buf + namelen + 1;
	datalen = buflen - namelen - 1;
	if (datalen == 0)
		goto invalid;
	/* sanity check */
	for (i = 0; i < (int)namelen; ++i)
		if (!isalpha(name[i]))
			goto invalid;
	if (strcmp(name, "caprights") == 0) {
		if (datalen != sizeof(cap_rights_t))
			goto invalid;
		memcpy(&rights, data, datalen);
		ktrcaprights(&rights);
	} else if (strcmp(name, "itimerval") == 0) {
		if (datalen != sizeof(struct itimerval))
			goto invalid;
		memcpy(&it, data, datalen);
		ktritimerval(&it);
	} else if (strcmp(name, "stat") == 0) {
		if (datalen != sizeof(struct stat))
			goto invalid;
		memcpy(&sb, data, datalen);
		ktrstat(&sb);
	} else if (strcmp(name, "sockaddr") == 0) {
		if (datalen > sizeof(ss))
			goto invalid;
		memcpy(&ss, data, datalen);
		if (datalen != ss.ss_len)
			goto invalid;
		ktrsockaddr((struct sockaddr *)&ss);
	} else {
		printf("unknown structure\n");
	}
	return;
invalid:
	printf("invalid record\n");
}

void
ktrcapfail(struct ktr_cap_fail *ktr)
{
	switch (ktr->cap_type) {
	case CAPFAIL_NOTCAPABLE:
		/* operation on fd with insufficient capabilities */
		printf("operation requires ");
		sysdecode_cap_rights(stdout, &ktr->cap_needed);
		printf(", descriptor holds ");
		sysdecode_cap_rights(stdout, &ktr->cap_held);
		break;
	case CAPFAIL_INCREASE:
		/* requested more capabilities than fd already has */
		printf("attempt to increase capabilities from ");
		sysdecode_cap_rights(stdout, &ktr->cap_held);
		printf(" to ");
		sysdecode_cap_rights(stdout, &ktr->cap_needed);
		break;
	case CAPFAIL_SYSCALL:
		/* called restricted syscall */
		printf("disallowed system call");
		break;
	case CAPFAIL_LOOKUP:
		/* absolute or AT_FDCWD path, ".." path, etc. */
		printf("restricted VFS lookup");
		break;
	default:
		printf("unknown capability failure: ");
		sysdecode_cap_rights(stdout, &ktr->cap_needed);
		printf(" ");
		sysdecode_cap_rights(stdout, &ktr->cap_held);
		break;
	}
	printf("\n");
}

void
ktrfault(struct ktr_fault *ktr)
{

	printf("0x%jx ", (uintmax_t)ktr->vaddr);
	print_mask_arg(sysdecode_vmprot, ktr->type);
	printf("\n");
}

void
ktrfaultend(struct ktr_faultend *ktr)
{
	const char *str;

	str = sysdecode_vmresult(ktr->result);
	if (str != NULL)
		printf("%s", str);
	else
		printf("<invalid=%d>", ktr->result);
	printf("\n");
}

void
ktrkevent(struct kevent *kev)
{

	printf("{ ident=");
	switch (kev->filter) {
	case EVFILT_READ:
	case EVFILT_WRITE:
	case EVFILT_VNODE:
	case EVFILT_PROC:
	case EVFILT_TIMER:
	case EVFILT_PROCDESC:
	case EVFILT_EMPTY:
		printf("%ju", (uintmax_t)kev->ident);
		break;
	case EVFILT_SIGNAL:
		print_signal(kev->ident);
		break;
	default:
		printf("%p", (void *)kev->ident);
	}
	printf(", filter=");
	print_integer_arg(sysdecode_kevent_filter, kev->filter);
	printf(", flags=");
	print_mask_arg0(sysdecode_kevent_flags, kev->flags);
	printf(", fflags=");
	sysdecode_kevent_fflags(stdout, kev->filter, kev->fflags,
	    decimal ? 10 : 16);
	printf(", data=%#jx, udata=%p }", (uintmax_t)kev->data, kev->udata);
}

void
ktrstructarray(struct ktr_struct_array *ksa, size_t buflen)
{
	struct kevent kev;
	char *name, *data;
	size_t namelen, datalen;
	int i;
	bool first;

	buflen -= sizeof(*ksa);
	for (name = (char *)(ksa + 1), namelen = 0;
	     namelen < buflen && name[namelen] != '\0';
	     ++namelen)
		/* nothing */;
	if (namelen == buflen)
		goto invalid;
	if (name[namelen] != '\0')
		goto invalid;
	/* sanity check */
	for (i = 0; i < (int)namelen; ++i)
		if (!isalnum(name[i]) && name[i] != '_')
			goto invalid;
	data = name + namelen + 1;
	datalen = buflen - namelen - 1;
	printf("struct %s[] = { ", name);
	first = true;
	for (; datalen >= ksa->struct_size;
	    data += ksa->struct_size, datalen -= ksa->struct_size) {
		if (!first)
			printf("\n             ");
		else
			first = false;
		if (strcmp(name, "kevent") == 0) {
			if (ksa->struct_size != sizeof(kev))
				goto bad_size;
			memcpy(&kev, data, sizeof(kev));
			ktrkevent(&kev);
		} else if (strcmp(name, "kevent_freebsd11") == 0) {
			struct kevent_freebsd11 kev11;

			if (ksa->struct_size != sizeof(kev11))
				goto bad_size;
			memcpy(&kev11, data, sizeof(kev11));
			memset(&kev, 0, sizeof(kev));
			kev.ident = kev11.ident;
			kev.filter = kev11.filter;
			kev.flags = kev11.flags;
			kev.fflags = kev11.fflags;
			kev.data = kev11.data;
			kev.udata = kev11.udata;
			ktrkevent(&kev);
#ifdef _WANT_KEVENT32
		} else if (strcmp(name, "kevent32") == 0) {
			struct kevent32 kev32;

			if (ksa->struct_size != sizeof(kev32))
				goto bad_size;
			memcpy(&kev32, data, sizeof(kev32));
			memset(&kev, 0, sizeof(kev));
			kev.ident = kev32.ident;
			kev.filter = kev32.filter;
			kev.flags = kev32.flags;
			kev.fflags = kev32.fflags;
#if BYTE_ORDER == BIG_ENDIAN
			kev.data = kev32.data2 | ((int64_t)kev32.data1 << 32);
#else
			kev.data = kev32.data1 | ((int64_t)kev32.data2 << 32);
#endif
			kev.udata = (void *)(uintptr_t)kev32.udata;
			ktrkevent(&kev);
		} else if (strcmp(name, "kevent32_freebsd11") == 0) {
			struct kevent32_freebsd11 kev32;

			if (ksa->struct_size != sizeof(kev32))
				goto bad_size;
			memcpy(&kev32, data, sizeof(kev32));
			memset(&kev, 0, sizeof(kev));
			kev.ident = kev32.ident;
			kev.filter = kev32.filter;
			kev.flags = kev32.flags;
			kev.fflags = kev32.fflags;
			kev.data = kev32.data;
			kev.udata = (void *)(uintptr_t)kev32.udata;
			ktrkevent(&kev);
#endif
		} else {
			printf("<unknown structure> }\n");
			return;
		}
	}
	printf(" }\n");
	return;
invalid:
	printf("invalid record\n");
	return;
bad_size:
	printf("<bad size> }\n");
	return;
}

void
usage(void)
{
	fprintf(stderr, "usage: kdump [-dEnlHRrSsTA] [-f trfile] "
	    "[-m maxdata] [-p pid] [-t trstr]\n");
	exit(1);
}
