/*	$OpenBSD: ptmget.c,v 1.4 2021/10/24 21:24:20 deraadt Exp $ */
/*
 *	Written by Bob Beck <beck@openbsd.org> 2004 Public Domain.
 *	Basic test to ensure /dev/ptm works, and what it returns
 * 	can be used via tty(4);
 */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/tty.h>
#include <sys/stat.h>

#include <err.h>
#include <fcntl.h>
#include <grp.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int
main(int argc, char *argv[])
{
	int fd;
	struct ptmget ptm;
	struct termios ti;
	struct stat sb;
	struct group *gr;
	gid_t ttygid;

	if ((gr = getgrnam("tty")) != NULL)
		ttygid = gr->gr_gid;
	else
		ttygid = 4;
	fd = open("/dev/ptm", O_RDWR);
	if (fd == -1)
		err(1, "Can't open /dev/ptm");
	if ((ioctl(fd, PTMGET, &ptm) == -1))
		err(1, "ioctl PTMGET failed");
	if ((tcgetattr(ptm.sfd, &ti) == -1))
		err(1, "tcgetattr failed on slave");
	if ((tcgetattr(ptm.cfd, &ti) == -1))
		err(1, "tcgetattr failed on master");
	if ((ioctl(ptm.sfd, TIOCSTOP) == -1))
		err(1, "ioctl TIOCSTOP failed on slave");
	if ((ioctl(ptm.cfd, TIOCSTOP) == -1))
		err(1, "ioctl TIOCSTOP failed on master");
	bzero(&sb, sizeof(sb));
	if ((stat(ptm.sn, &sb) == -1))
		err(1, "can't stat slave %s", ptm.sn);
	if (sb.st_mode != (S_IFCHR | S_IWUSR | S_IRUSR | S_IWGRP))
		errx(1, "Bad mode %o on %s, should be %o", sb.st_mode,
		     ptm.sn, (S_IFCHR | S_IWUSR | S_IRUSR | S_IWGRP));
	if (sb.st_gid != ttygid)
		errx(1, "%s gid is %d not the tty group(%d)", ptm.sn,
		     sb.st_gid, ttygid);
	if (sb.st_uid != geteuid())
		errx(1, "%s owned by %d, not the current user (%d)", ptm.sn,
		     sb.st_uid, geteuid());
	return(0);
}
