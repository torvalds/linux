/*	$OpenBSD: mail.local.c,v 1.43 2024/05/09 08:35:03 florian Exp $	*/

/*-
 * Copyright (c) 1996-1998 Theo de Raadt <deraadt@theos.com>
 * Copyright (c) 1996-1998 David Mazieres <dm@lcs.mit.edu>
 * Copyright (c) 1990 The Regents of the University of California.
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

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <sysexits.h>
#include <syslog.h>
#include <fcntl.h>
#include <netdb.h>
#include <pwd.h>
#include <time.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include "pathnames.h"
#include "mail.local.h"

int
main(int argc, char *argv[])
{
	struct passwd *pw;
	int ch, fd, eval, lockfile=1;
	uid_t uid;
	char *from;

	openlog("mail.local", LOG_PERROR, LOG_MAIL);

	from = NULL;
	while ((ch = getopt(argc, argv, "lLdf:r:")) != -1)
		switch (ch) {
		case 'd':		/* backward compatible */
			break;
		case 'f':
		case 'r':		/* backward compatible */
			if (from)
				merr(EX_USAGE, "multiple -f options");
			from = optarg;
			break;
		case 'l':
			lockfile=1;
			break;
		case 'L':
			lockfile=0;
			break;
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if (!*argv)
		usage();

	/*
	 * If from not specified, use the name from getlogin() if the
	 * uid matches, otherwise, use the name from the password file
	 * corresponding to the uid.
	 */
	uid = getuid();
	if (!from && (!(from = getlogin()) ||
	    !(pw = getpwnam(from)) || pw->pw_uid != uid))
		from = (pw = getpwuid(uid)) ? pw->pw_name : "???";

	fd = storemail(from);
	for (eval = 0; *argv; ++argv) {
		if ((ch = deliver(fd, *argv, lockfile)) != 0)
			eval = ch;
	}
	exit(eval);
}

int
storemail(char *from)
{
	FILE *fp = NULL;
	time_t tval;
	int fd, eline = 1;
	char *tbuf, *line = NULL, *cnow;
	size_t linesize = 0;
	ssize_t linelen;

	if ((tbuf = strdup(_PATH_LOCTMP)) == NULL)
		merr(EX_OSERR, "unable to allocate memory");
	if ((fd = mkstemp(tbuf)) == -1 || !(fp = fdopen(fd, "w+")))
		merr(EX_OSERR, "unable to open temporary file");
	(void)unlink(tbuf);
	free(tbuf);

	(void)time(&tval);
	cnow = ctime(&tval);
	(void)fprintf(fp, "From %s %s", from, cnow ? cnow : "?\n");

	while ((linelen = getline(&line, &linesize, stdin)) != -1) {
		if (line[linelen - 1] == '\n')
			line[linelen - 1] = '\0';
		if (line[0] == '\0')
			eline = 1;
		else {
			if (eline && !strncmp(line, "From ", 5))
				(void)putc('>', fp);
			eline = 0;
		}
		(void)fprintf(fp, "%s\n", line);
		if (ferror(fp))
			break;
	}
	free(line);

	/* Output a newline; note, empty messages are allowed. */
	(void)putc('\n', fp);
	(void)fflush(fp);
	if (ferror(fp))
		merr(EX_OSERR, "temporary file write error");
	return(fd);
}

int
deliver(int fd, char *name, int lockfile)
{
	struct stat sb, fsb;
	struct passwd *pw;
	int mbfd=-1, lfd=-1, rval=EX_OSERR;
	char biffmsg[100], buf[8*1024], path[PATH_MAX];
	off_t curoff;
	size_t off;
	ssize_t nr, nw;

	/*
	 * Disallow delivery to unknown names -- special mailboxes can be
	 * handled in the sendmail aliases file.
	 */
	if (!(pw = getpwnam(name))) {
		mwarn("unknown name: %s", name);
		return(EX_NOUSER);
	}

	(void)snprintf(path, sizeof path, "%s/%s", _PATH_MAILDIR, name);

	if (lockfile) {
		lfd = lockspool(name, pw);
		if (lfd == -1)
			return(EX_OSERR);
	}

	/* after this point, always exit via bad to remove lockfile */
retry:
	if (lstat(path, &sb)) {
		if (errno != ENOENT) {
			mwarn("%s: %s", path, strerror(errno));
			goto bad;
		}
		if ((mbfd = open(path, O_APPEND|O_CREAT|O_EXCL|O_WRONLY|O_EXLOCK,
		    S_IRUSR|S_IWUSR)) == -1) {
			if (errno == EEXIST) {
				/* file appeared since lstat */
				goto retry;
			} else {
				mwarn("%s: %s", path, strerror(errno));
				rval = EX_CANTCREAT;
				goto bad;
			}
		}
		/*
		 * Set the owner and group.  Historically, binmail repeated
		 * this at each mail delivery.  We no longer do this, assuming
		 * that if the ownership or permissions were changed there
		 * was a reason for doing so.
		 */
		if (fchown(mbfd, pw->pw_uid, pw->pw_gid) == -1) {
			mwarn("chown %u:%u: %s", pw->pw_uid, pw->pw_gid, name);
			goto bad;
		}
	} else {
		if (sb.st_nlink != 1 || !S_ISREG(sb.st_mode)) {
			mwarn("%s: linked or special file", path);
			goto bad;
		}
		if ((mbfd = open(path, O_APPEND|O_WRONLY|O_EXLOCK,
		    S_IRUSR|S_IWUSR)) == -1) {
			mwarn("%s: %s", path, strerror(errno));
			goto bad;
		}
		if (fstat(mbfd, &fsb) == -1) {
			/* relating error to path may be bad style */
			mwarn("%s: %s", path, strerror(errno));
			goto bad;
		}
		if (sb.st_dev != fsb.st_dev || sb.st_ino != fsb.st_ino) {
			mwarn("%s: changed after open", path);
			goto bad;
		}
		/* paranoia? */
		if (fsb.st_nlink != 1 || !S_ISREG(fsb.st_mode)) {
			mwarn("%s: linked or special file", path);
			rval = EX_CANTCREAT;
			goto bad;
		}
	}

	curoff = lseek(mbfd, 0, SEEK_END);
	(void)snprintf(biffmsg, sizeof biffmsg, "%s@%lld\n", name,
	    (long long)curoff);
	if (lseek(fd, 0, SEEK_SET) == (off_t)-1) {
		mwarn("temporary file: %s", strerror(errno));
		goto bad;
	}

	while ((nr = read(fd, buf, sizeof(buf))) > 0)
		for (off = 0; off < nr;  off += nw)
			if ((nw = write(mbfd, buf + off, nr - off)) == -1) {
				mwarn("%s: %s", path, strerror(errno));
				(void)ftruncate(mbfd, curoff);
				goto bad;
			}

	if (nr == 0) {
		rval = 0;
	} else {
		(void)ftruncate(mbfd, curoff);
		mwarn("temporary file: %s", strerror(errno));
	}

bad:
	if (lfd != -1)
		unlockspool();

	if (mbfd != -1) {
		(void)fsync(mbfd);		/* Don't wait for update. */
		(void)close(mbfd);		/* Implicit unlock. */
	}

	if (!rval)
		notifybiff(biffmsg);
	return(rval);
}

void
notifybiff(char *msg)
{
	static struct addrinfo *res0;
	struct addrinfo hints, *res;
	static int f = -1;
	size_t len;
	int error;

	if (res0 == NULL) {
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = PF_UNSPEC;
		hints.ai_socktype = SOCK_DGRAM;

		error = getaddrinfo("localhost", "biff", &hints, &res0);
		if (error) {
			/* Be silent if biff service not available. */
			if (error != EAI_SERVICE) {
				mwarn("localhost: %s", gai_strerror(error));
			}
			return;
		}
	}

	if (f == -1) {
		for (res = res0; res != NULL; res = res->ai_next) {
			f = socket(res->ai_family, res->ai_socktype,
			    res->ai_protocol);
			if (f != -1)
				break;
		}
	}
	if (f == -1) {
		mwarn("socket: %s", strerror(errno));
		return;
	}

	len = strlen(msg) + 1;	/* XXX */
	if (sendto(f, msg, len, 0, res->ai_addr, res->ai_addrlen) != len)
		mwarn("sendto biff: %s", strerror(errno));
}

static int lockfd = -1;
static pid_t lockpid = -1;

int
lockspool(const char *name, struct passwd *pw)
{
	int pfd[2];
	char ch;

	if (geteuid() == 0)
		return getlock(name, pw);

	/* If not privileged, open pipe to lockspool(1) instead */
	if (pipe2(pfd, O_CLOEXEC) == -1) {
		merr(EX_OSERR, "pipe: %s", strerror(errno));
		return -1;
	}

	signal(SIGPIPE, SIG_IGN);
	switch ((lockpid = fork())) {
	case -1:
		merr(EX_OSERR, "fork: %s", strerror(errno));
		return -1;
	case 0:
		/* child */
		close(pfd[0]);
		dup2(pfd[1], STDOUT_FILENO);
		execl(_PATH_LOCKSPOOL, "lockspool", (char *)NULL);
		merr(EX_OSERR, "execl: lockspool: %s", strerror(errno));
		/* NOTREACHED */
		break;
	default:
		/* parent */
		close(pfd[1]);
		lockfd = pfd[0];
		break;
	}

	if (read(lockfd, &ch, 1) != 1 || ch != '1') {
		unlockspool();
		merr(EX_OSERR, "lockspool: unable to get lock");
	}

	return lockfd;
}

void
unlockspool(void)
{
	if (lockpid != -1) {
		waitpid(lockpid, NULL, 0);
		lockpid = -1;
	} else {
		rellock();
	}
	close(lockfd);
	lockfd = -1;
}

void
usage(void)
{
	merr(EX_USAGE, "usage: mail.local [-Ll] [-f from] user ...");
}
