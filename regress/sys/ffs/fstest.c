/*	$OpenBSD: fstest.c,v 1.7 2021/12/13 16:56:49 deraadt Exp $	*/

/*
 * Copyright (c) 2006-2007 Pawel Jakub Dawidek <pjd@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/tools/regression/fstest/fstest.c,v 1.1 2007/01/17 01:42:07 pjd Exp $
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sysctl.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <grp.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <assert.h>

enum action {
	ACTION_OPEN,
	ACTION_CREATE,
	ACTION_UNLINK,
	ACTION_MKDIR,
	ACTION_RMDIR,
	ACTION_LINK,
	ACTION_SYMLINK,
	ACTION_RENAME,
	ACTION_MKFIFO,
	ACTION_CHMOD,
	ACTION_CHOWN,
	ACTION_LCHOWN,
	ACTION_CHFLAGS,
	ACTION_LCHFLAGS,
	ACTION_TRUNCATE,
	ACTION_STAT,
	ACTION_LSTAT,
};

#define	TYPE_NONE	0x0000
#define	TYPE_STRING	0x0001
#define	TYPE_NUMBER	0x0002
#define	TYPE_OPTIONAL	0x0100
#define	MAX_ARGS	8

struct syscall_desc {
	char *sd_name;
	enum action  sd_action;
	int sd_args[MAX_ARGS];
};

static struct syscall_desc syscalls[] = {
	{ "open", ACTION_OPEN, { TYPE_STRING, TYPE_STRING,
				 TYPE_NUMBER | TYPE_OPTIONAL, TYPE_NONE } },
	{ "create", ACTION_CREATE, { TYPE_STRING, TYPE_NUMBER, TYPE_NONE } },
	{ "unlink", ACTION_UNLINK, { TYPE_STRING, TYPE_NONE } },
	{ "mkdir", ACTION_MKDIR, { TYPE_STRING, TYPE_NUMBER, TYPE_NONE } },
	{ "rmdir", ACTION_RMDIR, { TYPE_STRING, TYPE_NONE } },
	{ "link", ACTION_LINK, { TYPE_STRING, TYPE_STRING, TYPE_NONE } },
	{ "symlink", ACTION_SYMLINK, { TYPE_STRING, TYPE_STRING, TYPE_NONE } },
	{ "rename", ACTION_RENAME, { TYPE_STRING, TYPE_STRING, TYPE_NONE } },
	{ "mkfifo", ACTION_MKFIFO, { TYPE_STRING, TYPE_NUMBER, TYPE_NONE } },
	{ "chmod", ACTION_CHMOD, { TYPE_STRING, TYPE_NUMBER, TYPE_NONE } },
	{ "chown", ACTION_CHOWN, { TYPE_STRING, TYPE_NUMBER,
				   TYPE_NUMBER, TYPE_NONE } },
	{ "lchown", ACTION_LCHOWN, { TYPE_STRING, TYPE_NUMBER,
				     TYPE_NUMBER, TYPE_NONE } },
	{ "chflags", ACTION_CHFLAGS, { TYPE_STRING, TYPE_STRING, TYPE_NONE } },
	{ "lchflags", ACTION_LCHFLAGS, { TYPE_STRING, TYPE_STRING,
					 TYPE_NONE } },
	{ "truncate", ACTION_TRUNCATE, { TYPE_STRING, TYPE_NUMBER,
					 TYPE_NONE } },
	{ "stat", ACTION_STAT, { TYPE_STRING, TYPE_STRING, TYPE_NONE } },
	{ "lstat", ACTION_LSTAT, { TYPE_STRING, TYPE_STRING, TYPE_NONE } },
	{ NULL, -1, { TYPE_NONE } }
};

struct flag {
	long long f_flag;
	char *f_str;
};

static struct flag open_flags[] = {
	{ O_RDONLY, "O_RDONLY" },
	{ O_WRONLY, "O_WRONLY" },
	{ O_RDWR, "O_RDWR" },
	{ O_NONBLOCK, "O_NONBLOCK" },
	{ O_APPEND, "O_APPEND" },
	{ O_CREAT, "O_CREAT" },
	{ O_TRUNC, "O_TRUNC" },
	{ O_EXCL, "O_EXCL" },
	{ O_SHLOCK, "O_SHLOCK" },
	{ O_EXLOCK, "O_EXLOCK" },
	{ O_FSYNC, "O_FSYNC" },
	{ O_SYNC, "O_SYNC" },
	{ O_NOFOLLOW, "O_NOFOLLOW" },
	{ O_NOCTTY, "O_NOCTTY" },
	{ 0, NULL }
};

static struct flag chflags_flags[] = {
	{ UF_NODUMP, "UF_NODUMP" },
	{ UF_IMMUTABLE, "UF_IMMUTABLE" },
	{ UF_APPEND, "UF_APPEND" },
	{ UF_OPAQUE, "UF_OPAQUE" },
	{ SF_ARCHIVED, "SF_ARCHIVED" },
	{ SF_IMMUTABLE, "SF_IMMUTABLE" },
	{ SF_APPEND, "SF_APPEND" },
	{ 0, NULL }
};

static const char *err2str(int error);
int use_appimm;		/* use the SF_APPEND and SF_IMMUTABLE chflags */

__dead static void
usage(void)
{
	fprintf(stderr, "usage: fstest [-u uid] [-g gid1[,gid2[...]]] syscall "
	    "args ...\n");
	exit(1);
}

static long long
str2flags(struct flag *tflags, char *sflags)
{
	long long flags = 0;
	unsigned int i;
	char *f;

	for (f = strtok(sflags, ","); f != NULL; f = strtok(NULL, ",")) {
		/* Support magic 'none' flag which just reset all flags. */
		if (strcmp(f, "none") == 0)
			return (0);
		for (i = 0; tflags[i].f_str != NULL; i++) {
			if (strcmp(tflags[i].f_str, f) == 0)
				break;
		}
		if (tflags[i].f_str == NULL) {
			fprintf(stderr, "unknown flag '%s'\n", f);
			exit(1);
		}
		flags |= tflags[i].f_flag;
	}
	return (flags);
}

static char *
flags2str(struct flag *tflags, long long flags)
{
	static char sflags[1024];
	unsigned int i;

	sflags[0] = '\0';
	for (i = 0; tflags[i].f_str != NULL; i++) {
		if (flags & tflags[i].f_flag) {
			if (sflags[0] != '\0')
				strlcat(sflags, ",", sizeof(sflags));
			strlcat(sflags, tflags[i].f_str, sizeof(sflags));
		}
	}
	if (sflags[0] == '\0')
		strlcpy(sflags, "none", sizeof(sflags));
	return (sflags);
}

static struct syscall_desc *
find_syscall(const char *name)
{
	int i;

	for (i = 0; syscalls[i].sd_name != NULL; i++) {
		if (strcmp(syscalls[i].sd_name, name) == 0)
			return (&syscalls[i]);
	}
	return (NULL);
}

static void
show_stat(struct stat *sp, const char *what)
{

	if (strcmp(what, "mode") == 0)
		printf("0%o", (unsigned int)(sp->st_mode & ALLPERMS));
	else if (strcmp(what, "inode") == 0)
		printf("%llu", (unsigned long long)sp->st_ino);
	else if (strcmp(what, "nlink") == 0)
		printf("%lld", (long long)sp->st_nlink);
	else if (strcmp(what, "uid") == 0)
		printf("%d", (int)sp->st_uid);
	else if (strcmp(what, "gid") == 0)
		printf("%d", (int)sp->st_gid);
	else if (strcmp(what, "size") == 0)
		printf("%lld", (long long)sp->st_size);
	else if (strcmp(what, "blocks") == 0)
		printf("%lld", (long long)sp->st_blocks);
	else if (strcmp(what, "atime") == 0)
		printf("%lld", (long long)sp->st_atime);
	else if (strcmp(what, "mtime") == 0)
		printf("%lld", (long long)sp->st_mtime);
	else if (strcmp(what, "ctime") == 0)
		printf("%lld", (long long)sp->st_ctime);
	else if (strcmp(what, "flags") == 0)
		printf("%s", flags2str(chflags_flags, sp->st_flags));
	else if (strcmp(what, "type") == 0) {
		switch (sp->st_mode & S_IFMT) {
		case S_IFIFO:
			printf("fifo");
			break;
		case S_IFCHR:
			printf("char");
			break;
		case S_IFDIR:
			printf("dir");
			break;
		case S_IFBLK:
			printf("block");
			break;
		case S_IFREG:
			printf("regular");
			break;
		case S_IFLNK:
			printf("symlink");
			break;
		case S_IFSOCK:
			printf("socket");
			break;
		default:
			printf("unknown");
			break;
		}
	} else {
		printf("unknown");
	}
}

static void
show_stats(struct stat *sp, char *what)
{
	const char *s = "";
	char *w;

	for (w = strtok(what, ","); w != NULL; w = strtok(NULL, ",")) {
		printf("%s", s);
		show_stat(sp, w);
		s = ",";
	}
	printf("\n");
}

static unsigned int
call_syscall(struct syscall_desc *scall, char *argv[])
{
	struct stat sb;
	long long flags;
	unsigned int i;
	char *endp;
	int rval;
	union {
		char *str;
		long long num;
	} args[MAX_ARGS];
	unsigned int ch_flags;

	/*
	 * Verify correctness of the arguments.
	 */
	for (i = 0; i < sizeof(args)/sizeof(args[0]); i++) {
		if (scall->sd_args[i] == TYPE_NONE) {
			if (argv[i] == NULL || strcmp(argv[i], ":") == 0)
				break;
			fprintf(stderr, "too many arguments [%s]\n", argv[i]);
			exit(1);
		} else {
			if (argv[i] == NULL || strcmp(argv[i], ":") == 0) {
				if (scall->sd_args[i] & TYPE_OPTIONAL)
					break;
				fprintf(stderr, "too few arguments\n");
				exit(1);
			}
			if (scall->sd_args[i] & TYPE_STRING) {
				if (strcmp(argv[i], "NULL") == 0)
					args[i].str = NULL;
				else if (strcmp(argv[i], "DEADCODE") == 0)
					args[i].str = (void *)0xdeadc0de;
				else
					args[i].str = argv[i];
			} else if (scall->sd_args[i] & TYPE_NUMBER) {
				args[i].num = strtoll(argv[i], &endp, 0);
				if (*endp != '\0' &&
				    !isspace((unsigned char)*endp)) {
					fprintf(stderr, "invalid argument %u, "
					    "number expected [%s]\n", i, endp);
					exit(1);
				}
			}
		}
	}
	/*
	 * Call the given syscall.
	 */
#define	NUM(n)	(args[(n)].num)
#define	STR(n)	(args[(n)].str)
	switch (scall->sd_action) {
	case ACTION_OPEN:
		flags = str2flags(open_flags, STR(1));
		if (flags & O_CREAT) {
			if (i == 2) {
				fprintf(stderr, "too few arguments\n");
				exit(1);
			}
			rval = open(STR(0), flags, (mode_t)NUM(2));
		} else {
			if (i == 3) {
				fprintf(stderr, "too many arguments\n");
				exit(1);
			}
			rval = open(STR(0), flags);
		}
		break;
	case ACTION_CREATE:
		rval = open(STR(0), O_CREAT | O_EXCL, NUM(1));
		if (rval >= 0)
			close(rval);
		break;
	case ACTION_UNLINK:
		rval = unlink(STR(0));
		break;
	case ACTION_MKDIR:
		rval = mkdir(STR(0), NUM(1));
		break;
	case ACTION_RMDIR:
		rval = rmdir(STR(0));
		break;
	case ACTION_LINK:
		rval = link(STR(0), STR(1));
		break;
	case ACTION_SYMLINK:
		rval = symlink(STR(0), STR(1));
		break;
	case ACTION_RENAME:
		rval = rename(STR(0), STR(1));
		break;
	case ACTION_MKFIFO:
		rval = mkfifo(STR(0), NUM(1));
		break;
	case ACTION_CHMOD:
		rval = chmod(STR(0), NUM(1));
		break;
	case ACTION_CHOWN:
		rval = chown(STR(0), NUM(1), NUM(2));
		break;
	case ACTION_LCHOWN:
		rval = lchown(STR(0), NUM(1), NUM(2));
		break;
	case ACTION_CHFLAGS:
		ch_flags = str2flags(chflags_flags, STR(1));
		if (!use_appimm)
			ch_flags &= ~(SF_APPEND|SF_IMMUTABLE);
		rval = chflags(STR(0), ch_flags);
		break;
	case ACTION_LCHFLAGS:
		ch_flags = str2flags(chflags_flags, STR(1));
		if (!use_appimm)
			ch_flags &= ~(SF_APPEND|SF_IMMUTABLE);
		rval = chflagsat(AT_FDCWD, STR(0), ch_flags,
		    AT_SYMLINK_NOFOLLOW);
		break;
	case ACTION_TRUNCATE:
		rval = truncate(STR(0), NUM(1));
		break;
	case ACTION_STAT:
		rval = stat(STR(0), &sb);
		if (rval == 0) {
			show_stats(&sb, STR(1));
			return (i);
		}
		break;
	case ACTION_LSTAT:
		rval = lstat(STR(0), &sb);
		if (rval == 0) {
			show_stats(&sb, STR(1));
			return (i);
		}
		break;
	default:
		fprintf(stderr, "unsupported syscall\n");
		exit(1);
	}
#undef STR
#undef NUM
	if (rval < 0) {
		printf("%s\n", err2str(errno));
		exit(1);
	}
	printf("0\n");
	return (i);
}

static void
set_gids(char *gids)
{
	gid_t *gidset;
	long ngroups;
	char *g, *endp;
	unsigned i;

	ngroups = sysconf(_SC_NGROUPS_MAX);
	assert(ngroups > 0);
	gidset = reallocarray(NULL, ngroups, sizeof(*gidset));
	assert(gidset != NULL);
	for (i = 0, g = strtok(gids, ","); g != NULL;
	    g = strtok(NULL, ","), i++) {
		if (i >= ngroups) {
			fprintf(stderr, "too many gids\n");
			exit(1);
		}
		gidset[i] = strtol(g, &endp, 0);
		if (*endp != '\0' && !isspace((unsigned char)*endp)) {
			fprintf(stderr, "invalid gid '%s' - number expected\n",
			    g);
			exit(1);
		}
	}
	if (setgroups(i, gidset) < 0) {
		fprintf(stderr, "cannot change groups: %s\n", strerror(errno));
		exit(1);
	}
	free(gidset);
}

int
main(int argc, char *argv[])
{
	struct syscall_desc *scall;
	unsigned int n;
	char *gids, *endp;
	int uid, umsk, ch;
	int mib[2];
	size_t len;
	int securelevel;

	uid = -1;
	gids = NULL;
	umsk = 0;

	while ((ch = getopt(argc, argv, "g:u:U:")) != -1) {
		switch(ch) {
		case 'g':
			gids = optarg;
			break;
		case 'u':
			uid = (int)strtol(optarg, &endp, 0);
			if (*endp != '\0' && !isspace((unsigned char)*endp)) {
				fprintf(stderr, "invalid uid '%s' - number "
				    "expected\n", optarg);
				exit(1);
			}
			break;
		case 'U':
			umsk = (int)strtol(optarg, &endp, 0);
			if (*endp != '\0' && !isspace((unsigned char)*endp)) {
				fprintf(stderr, "invalid umask '%s' - number "
				    "expected\n", optarg);
				exit(1);
			}
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc < 1) {
		fprintf(stderr, "too few arguments\n");
		usage();
	}

	if (gids != NULL)
		set_gids(gids);
	if (uid != -1) {
		if (setuid(uid) < 0) {
			fprintf(stderr, "cannot change uid: %s\n",
			    strerror(errno));
			exit(1);
		}
	}

	/*
	 * Find out if we should use the SF_IMMUTABLE and SF_APPEND flags;
	 * Since we run by default on kern.securelevel=1 these cause false
	 * positives.
	 */
	mib[0] = CTL_KERN;
	mib[1] = KERN_SECURELVL;
	len = sizeof(securelevel);
	if (sysctl(mib, 2, &securelevel, &len, NULL, 0) == -1) {
		fprintf(stderr, "cannot get kernel securelevel\n");
		exit(1);
	}
	if (securelevel == 0 || securelevel == -1)
		use_appimm = 1;
	else
		use_appimm = 0;

	/* Change umask to requested value or to 0, if not requested. */
	umask(umsk);

	for (;;) {
		scall = find_syscall(argv[0]);
		if (scall == NULL) {
			fprintf(stderr, "syscall '%s' not supported\n",
			    argv[0]);
			exit(1);
		}
		argc++;
		argv++;
		n = call_syscall(scall, argv);
		argc += n;
		argv += n;
		if (argv[0] == NULL)
			break;
		argc++;
		argv++;
	}

	exit(0);
}

static const char *
err2str(int error)
{
	static char errnum[8];

	switch (error) {
	case EPERM:
		return ("EPERM");
	case ENOENT:
		return ("ENOENT");
	case ESRCH:
		return ("ESRCH");
	case EINTR:
		return ("EINTR");
	case EIO:
		return ("EIO");
	case ENXIO:
		return ("ENXIO");
	case E2BIG:
		return ("E2BIG");
	case ENOEXEC:
		return ("ENOEXEC");
	case EBADF:
		return ("EBADF");
	case ECHILD:
		return ("ECHILD");
	case EDEADLK:
		return ("EDEADLK");
	case ENOMEM:
		return ("ENOMEM");
	case EACCES:
		return ("EACCES");
	case EFAULT:
		return ("EFAULT");
	case ENOTBLK:
		return ("ENOTBLK");
	case EBUSY:
		return ("EBUSY");
	case EEXIST:
		return ("EEXIST");
	case EXDEV:
		return ("EXDEV");
	case ENODEV:
		return ("ENODEV");
	case ENOTDIR:
		return ("ENOTDIR");
	case EISDIR:
		return ("EISDIR");
	case EINVAL:
		return ("EINVAL");
	case ENFILE:
		return ("ENFILE");
	case EMFILE:
		return ("EMFILE");
	case ENOTTY:
		return ("ENOTTY");
	case ETXTBSY:
		return ("ETXTBSY");
	case EFBIG:
		return ("EFBIG");
	case ENOSPC:
		return ("ENOSPC");
	case ESPIPE:
		return ("ESPIPE");
	case EROFS:
		return ("EROFS");
	case EMLINK:
		return ("EMLINK");
	case EPIPE:
		return ("EPIPE");
	case EDOM:
		return ("EDOM");
	case ERANGE:
		return ("ERANGE");
	case EAGAIN:
		return ("EAGAIN");
	case EINPROGRESS:
		return ("EINPROGRESS");
	case EALREADY:
		return ("EALREADY");
	case ENOTSOCK:
		return ("ENOTSOCK");
	case EDESTADDRREQ:
		return ("EDESTADDRREQ");
	case EMSGSIZE:
		return ("EMSGSIZE");
	case EPROTOTYPE:
		return ("EPROTOTYPE");
	case ENOPROTOOPT:
		return ("ENOPROTOOPT");
	case EPROTONOSUPPORT:
		return ("EPROTONOSUPPORT");
	case ESOCKTNOSUPPORT:
		return ("ESOCKTNOSUPPORT");
	case EOPNOTSUPP:
		return ("EOPNOTSUPP");
	case EPFNOSUPPORT:
		return ("EPFNOSUPPORT");
	case EAFNOSUPPORT:
		return ("EAFNOSUPPORT");
	case EADDRINUSE:
		return ("EADDRINUSE");
	case EADDRNOTAVAIL:
		return ("EADDRNOTAVAIL");
	case ENETDOWN:
		return ("ENETDOWN");
	case ENETUNREACH:
		return ("ENETUNREACH");
	case ENETRESET:
		return ("ENETRESET");
	case ECONNABORTED:
		return ("ECONNABORTED");
	case ECONNRESET:
		return ("ECONNRESET");
	case ENOBUFS:
		return ("ENOBUFS");
	case EISCONN:
		return ("EISCONN");
	case ENOTCONN:
		return ("ENOTCONN");
	case ESHUTDOWN:
		return ("ESHUTDOWN");
	case ETOOMANYREFS:
		return ("ETOOMANYREFS");
	case ETIMEDOUT:
		return ("ETIMEDOUT");
	case ECONNREFUSED:
		return ("ECONNREFUSED");
	case ELOOP:
		return ("ELOOP");
	case ENAMETOOLONG:
		return ("ENAMETOOLONG");
	case EHOSTDOWN:
		return ("EHOSTDOWN");
	case EHOSTUNREACH:
		return ("EHOSTUNREACH");
	case ENOTEMPTY:
		return ("ENOTEMPTY");
	case EPROCLIM:
		return ("EPROCLIM");
	case EUSERS:
		return ("EUSERS");
	case EDQUOT:
		return ("EDQUOT");
	case ESTALE:
		return ("ESTALE");
	case EREMOTE:
		return ("EREMOTE");
	case EBADRPC:
		return ("EBADRPC");
	case ERPCMISMATCH:
		return ("ERPCMISMATCH");
	case EPROGUNAVAIL:
		return ("EPROGUNAVAIL");
	case EPROGMISMATCH:
		return ("EPROGMISMATCH");
	case EPROCUNAVAIL:
		return ("EPROCUNAVAIL");
	case ENOLCK:
		return ("ENOLCK");
	case ENOSYS:
		return ("ENOSYS");
	case EFTYPE:
		return ("EFTYPE");
	case EAUTH:
		return ("EAUTH");
	case ENEEDAUTH:
		return ("ENEEDAUTH");
	case EILSEQ:
		return ("EILSEQ");
	case ENOATTR:
		return ("ENOATTR");
	default:
		snprintf(errnum, sizeof(errnum), "%d", error);
		return (errnum);
	}
}
