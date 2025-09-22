/*	$OpenBSD: access.c,v 1.1 2014/04/27 22:18:25 guenther Exp $	*/
/*
 *	Written by Philip Guenther <guenther@openbsd.org> 2014 Public Domain.
 */

#include <sys/stat.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define	UID_YES		991
#define	UID_NO		990
#define	GID_YES		991
#define	GID_NO		990

char dir[] = "testdir";
char exists[] = "testdir/testfile";
char r_r_exists[] = "testdir/testfile_r_r";
char r_w_exists[] = "testdir/testfile_r_w";
char w_r_exists[] = "testdir/testfile_w_r";
char w_w_exists[] = "testdir/testfile_w_w";
char x_x_exists[] = "testdir/testfile_x_x";
char noexists[] = "testdir/nosuchfile";

char temp[] = "/tmp/accessXXXXXXXXX";

struct tests {
	int	err, eaccess;
	uid_t	ruid, euid;
	gid_t	rgid, egid;
	int	amode;
	const	char *filename;
} tests[] = {
 /* RETURNS   EACC RUID     EUID     RGID     EGID      AMODE  FILENAME */
 /* negative tests */
 /* unable to search through the directory */
 { EACCES,	0, UID_NO,  UID_NO,  GID_NO,  GID_NO,	F_OK,	exists },
 { EACCES,	0, UID_NO,  UID_YES, GID_NO,  GID_NO,	F_OK,	exists },
 { EACCES,	0, UID_NO,  UID_NO,  GID_NO,  GID_YES,	F_OK,	exists },
 { EACCES,	0, UID_NO,  UID_YES, GID_NO,  GID_YES,	F_OK,	exists },
 { EACCES,	1, UID_NO,  UID_NO,  GID_NO,  GID_NO,	F_OK,	exists },
 { EACCES,	1, UID_YES, UID_NO,  GID_NO,  GID_NO,	F_OK,	exists },
 { EACCES,	1, UID_NO,  UID_NO,  GID_YES, GID_NO,	F_OK,	exists },
 { EACCES,	1, UID_YES, UID_NO,  GID_YES, GID_NO,	F_OK,	exists },
 /* can search to it, but the file ain't there */
 { ENOENT,	0, UID_YES, UID_NO,  GID_NO,  GID_NO,	F_OK,	noexists },
 { ENOENT,	0, UID_NO,  UID_NO,  GID_YES, GID_NO,	F_OK,	noexists },
 { ENOENT,	0, UID_YES, UID_NO,  GID_YES, GID_NO,	F_OK,	noexists },
 { ENOENT,	1, UID_NO,  UID_YES, GID_NO,  GID_NO,	F_OK,	noexists },
 { ENOENT,	1, UID_NO,  UID_NO,  GID_NO,  GID_YES,	F_OK,	noexists },
 { ENOENT,	1, UID_NO,  UID_YES, GID_NO,  GID_YES,	F_OK,	noexists },
 /* can search to it, but the file doesn't have read perm */
 { EACCES,	0, UID_YES, UID_NO,  GID_NO,  GID_NO,	R_OK,	w_w_exists },
 { EACCES,	0, UID_NO,  UID_NO,  GID_YES, GID_NO,	R_OK,	w_w_exists },
 { EACCES,	0, UID_YES, UID_NO,  GID_YES, GID_NO,	R_OK,	w_w_exists },
 { EACCES,	1, UID_NO,  UID_YES, GID_NO,  GID_NO,	R_OK,	w_w_exists },
 { EACCES,	1, UID_NO,  UID_NO,  GID_NO,  GID_YES,	R_OK,	w_w_exists },
 /* can search to it, but the file doesn't have the right read perm */
 { EACCES,	0, UID_YES, UID_NO,  GID_NO,  GID_NO,	R_OK,	w_r_exists },
 { EACCES,	0, UID_NO,  UID_NO,  GID_YES, GID_NO,	R_OK,	r_w_exists },
 { EACCES,	1, UID_NO,  UID_YES, GID_NO,  GID_NO,	R_OK,	w_r_exists },
 { EACCES,	1, UID_NO,  UID_NO,  GID_NO,  GID_YES,	R_OK,	r_w_exists },
 /* if correct user, then group perms are ignored */
 { EACCES,	0, UID_YES, UID_NO,  GID_YES, GID_NO,	R_OK,	w_r_exists },
 { EACCES,	1, UID_NO,  UID_YES, GID_NO,  GID_YES,	R_OK,	w_r_exists },
 { EACCES,	0, UID_YES, UID_YES, GID_YES, GID_YES,	R_OK,	w_r_exists },
 { EACCES,	1, UID_YES, UID_YES, GID_YES, GID_YES,	R_OK,	w_r_exists },

 /* positive tests */
 { 0,		0, UID_YES, UID_NO,  GID_NO,  GID_NO,	R_OK,	r_w_exists },
 { 0,		0, UID_NO,  UID_NO,  GID_YES, GID_NO,	R_OK,	w_r_exists },
 { 0,		0, UID_YES, UID_NO,  GID_YES, GID_NO,	R_OK,	r_w_exists },
 { 0,		0, UID_YES, UID_NO,  GID_YES, GID_NO,	R_OK,	r_r_exists },
 { 0,		1, UID_NO,  UID_YES, GID_NO,  GID_NO,	R_OK,	r_w_exists },
 { 0,		1, UID_NO,  UID_NO,  GID_NO,  GID_YES,	R_OK,	w_r_exists },
 { 0,		1, UID_NO,  UID_YES, GID_NO,  GID_YES,	R_OK,	r_w_exists },
 { 0,		1, UID_NO,  UID_YES, GID_NO,  GID_YES,	R_OK,	r_r_exists },

 { 0,		0, UID_YES, UID_YES, GID_YES, GID_YES,	R_OK,	r_w_exists },
 { 0,		0, UID_YES, UID_YES, GID_YES, GID_YES,	R_OK,	r_r_exists },
 { 0,		1, UID_YES, UID_YES, GID_YES, GID_YES,	R_OK,	r_w_exists },
 { 0,		1, UID_YES, UID_YES, GID_YES, GID_YES,	R_OK,	r_r_exists },

 { 0 }
};

static void
prepfile(const char *filename, mode_t mode)
{
	int fd;

	if ((fd = open(filename, O_WRONLY|O_CREAT, 600)) < 0)
		err(1, "open %s", filename);
	close(fd);
	if (chown(filename, UID_YES, GID_YES))
		err(1, "chown %s %d:%d", filename, UID_YES, GID_YES);
	if (chmod(filename, mode))
		err(1, "chmod %s %o", filename, mode);
}

static void
docleanup(void)
{
	setresuid(0, 0, 0);
	remove(exists);
	remove(r_r_exists);
	remove(r_w_exists);
	remove(w_r_exists);
	remove(w_w_exists);
	remove(x_x_exists);
	remove(dir);
	chdir("/");
	remove(temp);
}

int
main(int argc, char *argv[])
{	
	char buf[200];
	struct tests *t;
	int ret, result;
	gid_t supp_group = GID_NO;

	if (geteuid() != 0) {
		if (getuid() != 0)
			errx(0, "must be run as root");
		else if (setuid(0))
			err(1, "setuid");
	}
	if (setgroups(1, &supp_group))
		err(1, "setgroups");

	if (mkdtemp(temp) == NULL)
		err(1, "mkdtemp");

	if (chdir(temp)) {
		ret = errno;
		remove(temp);
		errc(1, ret, "chdir");
	}
	if (chmod(temp, 0755))
		err(1, "chmod %s %o", temp, 0755);

	atexit(docleanup);

	umask(0);
	if (mkdir(dir, 0750))
		err(1, "mkdir");
	prepfile(exists, 0);
	prepfile(r_r_exists, 0440);
	prepfile(r_w_exists, 0420);
	prepfile(w_r_exists, 0240);
	prepfile(w_w_exists, 0220);
	prepfile(x_x_exists, 0110);
	if (chown(dir, UID_YES, GID_YES))
		err(1, "chown %s %d:%d", dir, UID_YES, GID_YES);

	result = 0;
	for (t = tests; t->filename != NULL; t++) {
		if (setresgid(t->rgid, t->egid, 0))
			err(1, "setresgid");
		if (setresuid(t->ruid, t->euid, 0))
			err(1, "setresuid");
		ret = faccessat(AT_FDCWD, t->filename, t->amode,
		    t->eaccess ? AT_EACCESS : 0);
		if (ret) {
			ret = errno;
			strerror_r(ret, buf, sizeof buf);
		}
		if (ret != t->err) {
			result = 1;
			warnx("uid %d/%d gid %d/%d mode %d eaccess %d %s:"
			    " %s instead of %s",
			    t->ruid, t->euid, t->rgid, t->egid,
			    t->amode, t->eaccess, t->filename,
			    ret ? buf : "success",
			    t->err ? strerror(t->err) : "success");
		}
		if (setresuid(0, 0, 0))
			err(1, "setresuid restore");
	}

	return (result);
}
