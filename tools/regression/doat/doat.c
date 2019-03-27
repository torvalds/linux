/*-
 * Copyright (c) 2007 Roman Divacky
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void cleanup(void);
void setup(void);
void setup_once(void);

union param {
	int		i;
	const char	*cp;
	mode_t		m;
	dev_t		d;
	void		*vp;
	uid_t		u;
	gid_t		g;
	const char	**cpp;
};

struct testcase {
	int		result;
	union param	params[5];	/* no *at syscall with more than 5 params */
};

struct test {
	int	syscall;
	int	num_of_cases;
	const char *name;
	struct testcase	tests[10];	/* no more than 10 tests */
	
};

struct test *tests;
#define	NUM_OF_TESTS	14		/* we dont want the fexecve test to run */

char *absolute_path = NULL;
const char *relative_path = "tmp/";
const char *not_dir_path = "/bin/date";

const char *file = "foo";
char *absolute_file = NULL;
char *relative_file = NULL;
const char *symlinkf = "link";
const char *newlink = "nlink1";
const char *newlink2 = "nlink2";
const char *newlink3 = "nlink3";
const char *newdir = "newdir";
const char *fifo = "fifo";
const char *nod = "nod";
const char *newfile = "newfile";
const char *newslink = "nslink1";

bool dir_exist = false;
bool file_exist = false;
bool link_exist = false;

int rel_fd, abs_fd, notd_fd, exec_fd;

struct timeval times[2];
struct stat buf;
const char *pargv[2] = { "/bin/date", NULL };
#define	PATH_MAX	1024
char cbuf[PATH_MAX];

void
setup(void)
{
	int i, error;
	struct stat sb;
	size_t len;

	tests = calloc(NUM_OF_TESTS + 1, sizeof(struct test));
	if (tests == NULL) {
		perror("");
		exit(0);		
	}

	absolute_path = (char *)getcwd(NULL, 0);
	if (absolute_path == NULL) {
		perror("getcwd");
		exit(0);
	}

	len = strlen(absolute_path);
	absolute_path = realloc(absolute_path,
	    len + 1 + strlen(relative_path) + 1);
	if (absolute_path == NULL) {
		perror("realloc");
		exit(0);
	}

	absolute_path[len] = '/';
	strcpy(absolute_path + len + 1, relative_path);

	absolute_file = malloc(strlen(absolute_path) + 1 + strlen(file));
	bzero(absolute_file, strlen(absolute_path) + 1 + strlen(file));
	if (absolute_file == NULL) {
		perror("malloc");
		exit(0);
	}
	strcpy(absolute_file, absolute_path);
	absolute_file[strlen(absolute_file)] = '/';
	strcpy(absolute_file + strlen(absolute_path), file);

	relative_file = malloc(strlen(relative_path) + 1 + strlen(file));
	bzero(relative_file, strlen(relative_path) + 1 + strlen(file));
	if (relative_file == NULL) {
		perror("malloc");
		exit(0);
	}
	strcpy(relative_file, relative_path);
	relative_file[strlen(relative_file)] = '/';
	strcpy(relative_file + strlen(relative_path), file);

	error = mkdir(relative_path, 0700);
	dir_exist = (errno == EEXIST);
	if (error && errno != EEXIST) {
		perror("tmp");
		exit(0);
	}

	error = stat("tmp/foo", &sb);
	file_exist = (errno != ENOENT);
	i = open("tmp/foo", O_RDONLY | O_CREAT, 0666);
	if (i == -1) {
		perror("foo");
		exit(0);
	}

	rel_fd = open(relative_path, O_RDONLY);
	if (rel_fd == -1) {
		perror("relative path");
		exit(0);
	}

	abs_fd = open(absolute_path, O_RDONLY);
	if (abs_fd == -1) {
		perror("absolute path");
		exit(0);
	}

	notd_fd = open(not_dir_path, O_RDONLY);
	if (notd_fd == -1) {
		perror("not a directory");
		exit(0);
	}

	exec_fd = open(not_dir_path, O_RDONLY);
	if (exec_fd == -1) {
		perror("not a directory");
		exit(0);
	}

	error = symlink(absolute_file, symlinkf);
	link_exist = (errno == EEXIST);
	if (error && errno != EEXIST) {
		perror("symlink");
		exit(0);
	}
	
	/* faccessat */
	tests[0].syscall = SYS_faccessat;
	tests[0].num_of_cases = 6;
	tests[0].name = "faccessat";
	tests[0].tests[0].result = EBADF;
	tests[0].tests[0].params[0].i = 106;	/* invalid fd */
	tests[0].tests[0].params[1].cp = relative_path;
	tests[0].tests[0].params[2].m = 0;
	tests[0].tests[0].params[3].i = 0;
	tests[0].tests[1].result = EBADF;
	tests[0].tests[1].params[0].i = 106;	/* invalid fd */
	tests[0].tests[1].params[1].cp = relative_path;
	tests[0].tests[1].params[2].m = 0;
	tests[0].tests[1].params[3].i = AT_EACCESS;
	tests[0].tests[2].result = EINVAL;
	tests[0].tests[2].params[0].i = rel_fd;
	tests[0].tests[2].params[1].cp = absolute_path;
	tests[0].tests[2].params[2].m = 0;
	tests[0].tests[2].params[3].i = 123;	/* invalid flag */
	tests[0].tests[3].result = ENOTDIR;
	tests[0].tests[3].params[0].i = notd_fd;
	tests[0].tests[3].params[1].cp = relative_file;
	tests[0].tests[3].params[2].m = 0;
	tests[0].tests[3].params[3].i = 0;
	tests[0].tests[4].result = 0;
	tests[0].tests[4].params[0].i = rel_fd;
	tests[0].tests[4].params[1].cp = file;
	tests[0].tests[4].params[2].m = 0;
	tests[0].tests[4].params[3].i = 0;
	tests[0].tests[5].result = 0;
	tests[0].tests[5].params[0].i = rel_fd;
	tests[0].tests[5].params[1].cp = file;
	tests[0].tests[5].params[2].m = 0;
	tests[0].tests[5].params[3].i = AT_EACCESS;
	tests[0].tests[6].result = 0;
	tests[0].tests[6].params[0].i = 106;	/* invalid fd */
	tests[0].tests[6].params[1].cp = absolute_path;
	tests[0].tests[6].params[2].m = 0;
	tests[0].tests[6].params[3].i = 0;

	/* fchmodat */
	tests[1].syscall = SYS_fchmodat;
	tests[1].num_of_cases = 6;
	tests[1].name = "fchmodat";
	tests[1].tests[0].result = EBADF;
	tests[1].tests[0].params[0].i = 106;	/* invalid fd */
	tests[1].tests[0].params[1].cp = relative_path;
	tests[1].tests[0].params[2].m = 33190;
	tests[1].tests[0].params[3].i = 0;
	tests[1].tests[1].result = EINVAL;
	tests[1].tests[1].params[0].i = rel_fd;
	tests[1].tests[1].params[1].cp = absolute_path;
	tests[1].tests[1].params[2].m = 33190;	/* mode 646 translated */
	tests[1].tests[1].params[3].i = 123;	/* invalid flag */
	tests[1].tests[2].result = ENOTDIR;
	tests[1].tests[2].params[0].i = notd_fd;
	tests[1].tests[2].params[1].cp = relative_file;
	tests[1].tests[2].params[2].m = 33190;
	tests[1].tests[2].params[3].i = 0;
	tests[1].tests[3].result = 0;
	tests[1].tests[3].params[0].i = notd_fd;
	tests[1].tests[3].params[1].cp = absolute_file;
	tests[1].tests[3].params[2].m = 33190;
	tests[1].tests[3].params[3].i = 0;
	tests[1].tests[4].result = 0;
	tests[1].tests[4].params[0].i = AT_FDCWD;
	tests[1].tests[4].params[1].cp = symlinkf;
	tests[1].tests[4].params[2].m = 33190;
	tests[1].tests[4].params[3].i = AT_SYMLINK_NOFOLLOW;
	tests[1].tests[5].result = 0;
	tests[1].tests[5].params[0].i = rel_fd;
	tests[1].tests[5].params[1].cp = file;
	tests[1].tests[5].params[2].m = 33190;
	tests[1].tests[5].params[3].i = 0;

	/* fchownat */
	tests[2].syscall = SYS_fchownat;
	tests[2].num_of_cases = 6;
	tests[2].name = "fchownat";
	tests[2].tests[0].result = EBADF;
	tests[2].tests[0].params[0].i = 106;	/* invalid fd */
	tests[2].tests[0].params[1].cp = relative_file;
	tests[2].tests[0].params[2].u = 65534;
	tests[2].tests[0].params[3].g = 65534;
	tests[2].tests[0].params[4].i = 0;
	tests[2].tests[1].result = EINVAL;
	tests[2].tests[1].params[0].i = rel_fd;
	tests[2].tests[1].params[1].cp = file;
	tests[2].tests[1].params[2].u = 65534;
	tests[2].tests[1].params[3].g = 65534;
	tests[2].tests[1].params[4].i = 123;	/* invalid flag */
	tests[2].tests[2].result = ENOTDIR;
	tests[2].tests[2].params[0].i = notd_fd;
	tests[2].tests[2].params[1].cp = relative_file;
	tests[2].tests[2].params[2].u = 65534;
	tests[2].tests[2].params[3].g = 65534;
	tests[2].tests[2].params[4].i = 0;
	tests[2].tests[3].result = 0;
	tests[2].tests[3].params[0].i = notd_fd;
	tests[2].tests[3].params[1].cp = absolute_file;
	tests[2].tests[3].params[2].u = 65534;
	tests[2].tests[3].params[3].g = 65534;
	tests[2].tests[3].params[4].i = 0;
	tests[2].tests[4].result = 0;
	tests[2].tests[4].params[0].i = AT_FDCWD;
	tests[2].tests[4].params[1].cp = symlinkf;
	tests[2].tests[4].params[2].u = 65534;
	tests[2].tests[4].params[3].g = 65534;
	tests[2].tests[4].params[4].i = AT_SYMLINK_NOFOLLOW;
	tests[2].tests[5].result = 0;
	tests[2].tests[5].params[0].i = rel_fd;
	tests[2].tests[5].params[1].cp = file;
	tests[2].tests[5].params[2].u = 0;
	tests[2].tests[5].params[3].g = 0;
	tests[2].tests[5].params[4].i = 0;

	/* fstatat */
	tests[3].syscall = SYS_fstatat;
	tests[3].num_of_cases = 5;
	tests[3].name = "fstatat";
	tests[3].tests[0].result = EBADF;
	tests[3].tests[0].params[0].i = 106;	/* invalid fd */
	tests[3].tests[0].params[1].cp = relative_file;
	tests[3].tests[0].params[2].vp = &buf;
	tests[3].tests[0].params[3].i = 0;
	tests[3].tests[1].result = EINVAL;
	tests[3].tests[1].params[0].i = rel_fd;
	tests[3].tests[1].params[1].cp = relative_file;
	tests[3].tests[1].params[2].vp = &buf;
	tests[3].tests[1].params[3].i = 123;	/* invalid flags */
	tests[3].tests[2].result = ENOTDIR;
	tests[3].tests[2].params[0].i = notd_fd;
	tests[3].tests[2].params[1].cp = relative_file;
	tests[3].tests[2].params[2].vp = &buf;
	tests[3].tests[2].params[3].i = 0;
	tests[3].tests[2].result = 0; 
	tests[3].tests[2].params[0].i = rel_fd;
	tests[3].tests[2].params[1].cp = file;
	tests[3].tests[2].params[2].vp = &buf;
	tests[3].tests[2].params[3].i = 0;
	tests[3].tests[3].result = 0; 
	tests[3].tests[3].params[0].i = AT_FDCWD;
	tests[3].tests[3].params[1].cp = symlinkf;
	tests[3].tests[3].params[2].vp = &buf;
	tests[3].tests[3].params[3].i = AT_SYMLINK_NOFOLLOW;
	tests[3].tests[4].result = 0; 
	tests[3].tests[4].params[0].i = notd_fd;
	tests[3].tests[4].params[1].cp = absolute_file;
	tests[3].tests[4].params[2].vp = &buf;
	tests[3].tests[4].params[3].i = 0;

	/* futimesat */
	tests[4].syscall = SYS_futimesat;
	tests[4].num_of_cases = 4;
	tests[4].name = "futimesat";
	tests[4].tests[0].result = EBADF;
	tests[4].tests[0].params[0].i = 106;	/* invalid fd */
	tests[4].tests[0].params[1].cp = relative_file;
	tests[4].tests[0].params[2].vp = times;
	tests[4].tests[1].result = ENOTDIR;
	tests[4].tests[1].params[0].i = notd_fd;
	tests[4].tests[1].params[1].cp = relative_file;
	tests[4].tests[1].params[2].vp = times;
	tests[4].tests[2].result = 0;
	tests[4].tests[2].params[0].i = rel_fd;
	tests[4].tests[2].params[1].cp = file;
	tests[4].tests[2].params[2].vp = times;
	tests[4].tests[3].result = 0;
	tests[4].tests[3].params[0].i = notd_fd;
	tests[4].tests[3].params[1].cp = absolute_file;
	tests[4].tests[3].params[2].vp = times;

	/* linkat */
	tests[5].syscall = SYS_linkat;
	tests[5].num_of_cases = 7;
	tests[5].name = "linkat";
	tests[5].tests[0].result = EBADF;
	tests[5].tests[0].params[0].i = 106;	/* invalid fd */
	tests[5].tests[0].params[1].cp = relative_file;
	tests[5].tests[0].params[2].i = AT_FDCWD;
	tests[5].tests[0].params[3].cp = newlink;
	tests[5].tests[0].params[4].i = 0;
	tests[5].tests[1].result = EBADF;
	tests[5].tests[1].params[0].i = AT_FDCWD;
	tests[5].tests[1].params[1].cp = relative_file;
	tests[5].tests[1].params[2].i = 106;	/* invalid fd */
	tests[5].tests[1].params[3].cp = newlink;
	tests[5].tests[1].params[4].i = 0;
	tests[5].tests[2].result = EINVAL;
	tests[5].tests[2].params[0].i = rel_fd;
	tests[5].tests[2].params[1].cp = relative_file;
	tests[5].tests[2].params[2].i = AT_FDCWD;
	tests[5].tests[2].params[3].cp = newlink;
	tests[5].tests[2].params[4].i = 123;	/* invalid flag */
	tests[5].tests[3].result = ENOTDIR;
	tests[5].tests[3].params[0].i = notd_fd;
	tests[5].tests[3].params[1].cp = relative_file;
	tests[5].tests[3].params[2].i = AT_FDCWD;
	tests[5].tests[3].params[3].cp = newlink;
	tests[5].tests[3].params[4].i = 0;
	tests[5].tests[4].result = 0;
	tests[5].tests[4].params[0].i = rel_fd;
	tests[5].tests[4].params[1].cp = file;
	tests[5].tests[4].params[2].i = rel_fd;
	tests[5].tests[4].params[3].cp = newlink;
	tests[5].tests[4].params[4].i = 0;
	tests[5].tests[5].result = 0;
	tests[5].tests[5].params[0].i = AT_FDCWD;
	tests[5].tests[5].params[1].cp = symlinkf;
	tests[5].tests[5].params[2].i = rel_fd;
	tests[5].tests[5].params[3].cp = newlink2;
	tests[5].tests[5].params[4].i = 0;
	tests[5].tests[6].result = 0;
	tests[5].tests[6].params[0].i = AT_FDCWD;
	tests[5].tests[6].params[1].cp = symlinkf;
	tests[5].tests[6].params[2].i = rel_fd;
	tests[5].tests[6].params[3].cp = newlink3;
	tests[5].tests[6].params[4].i = AT_SYMLINK_FOLLOW;

	/* mkdirat */
	tests[6].syscall = SYS_mkdirat;
	tests[6].num_of_cases = 3;
	tests[6].name = "mkdirat";
	tests[6].tests[0].result = EBADF;
	tests[6].tests[0].params[0].i = 106;	/* invalid fd */
	tests[6].tests[0].params[1].cp = relative_file;
	tests[6].tests[0].params[2].m = 33190;
	tests[6].tests[1].result = ENOTDIR;
	tests[6].tests[1].params[0].i = notd_fd;
	tests[6].tests[1].params[1].cp = relative_file;
	tests[6].tests[1].params[2].m = 33190;
	tests[6].tests[2].result = 0;
	tests[6].tests[2].params[0].i = rel_fd;
	tests[6].tests[2].params[1].cp = newdir;
	tests[6].tests[2].params[2].m = 33190;

	/* mkfifoat */
	tests[7].syscall = SYS_mkfifoat;
	tests[7].num_of_cases = 3;
	tests[7].name = "mkfifoat";
	tests[7].tests[0].result = EBADF;
	tests[7].tests[0].params[0].i = 107;	/* invalid fd */
	tests[7].tests[0].params[1].cp = relative_file;
	tests[7].tests[0].params[2].m = 33190;
	tests[7].tests[1].result = ENOTDIR;
	tests[7].tests[1].params[0].i = notd_fd;
	tests[7].tests[1].params[1].cp = relative_file;
	tests[7].tests[1].params[2].m = 33190;
	tests[7].tests[2].result = 0;
	tests[7].tests[2].params[0].i = rel_fd;
	tests[7].tests[2].params[1].cp = fifo;
	tests[7].tests[2].params[2].m = 33190;

	/* mknodat */
	tests[8].syscall = SYS_mknodat;
	tests[8].num_of_cases = 3;
	tests[8].name = "mknodat";
	tests[8].tests[0].result = EBADF;
	tests[8].tests[0].params[0].i = 108;	/* invalid fd */
	tests[8].tests[0].params[1].cp = relative_file;
	tests[8].tests[0].params[2].m = 0666 | S_IFCHR;
	tests[8].tests[0].params[3].d = 15;
	tests[8].tests[1].result = ENOTDIR;
	tests[8].tests[1].params[0].i = notd_fd;
	tests[8].tests[1].params[1].cp = relative_file;
	tests[8].tests[1].params[2].m = 0666 | S_IFCHR;
	tests[8].tests[1].params[3].d = 15;
	tests[8].tests[2].result = 0;
	tests[8].tests[2].params[0].i = rel_fd;
	tests[8].tests[2].params[1].cp = nod;
	tests[8].tests[2].params[2].m = 0666 | S_IFCHR;
	tests[8].tests[2].params[3].d = 2570;

	/* openat */
	tests[9].syscall = SYS_openat;
	tests[9].num_of_cases = 5;
	tests[9].name = "openat";
	tests[9].tests[0].result = EBADF;
	tests[9].tests[0].params[0].i = 106;	/* invalid fd */
	tests[9].tests[0].params[1].cp = relative_file;
	tests[9].tests[0].params[2].i = O_RDONLY;
	tests[9].tests[0].params[3].i = 0666;
	tests[9].tests[1].result = ENOTDIR;
	tests[9].tests[1].params[0].i = notd_fd;
	tests[9].tests[1].params[1].cp = relative_file;
	tests[9].tests[1].params[2].i = O_RDONLY;
	tests[9].tests[1].params[3].i = 0666;
	tests[9].tests[2].result = 8;		/* hardcoded fd */
	tests[9].tests[2].params[0].i = rel_fd;
	tests[9].tests[2].params[1].cp = file;
	tests[9].tests[2].params[2].i = O_RDONLY;
	tests[9].tests[2].params[3].i = 0400;
	tests[9].tests[3].result = 9;		/* hardcoded fd */
	tests[9].tests[3].params[0].i = notd_fd;
	tests[9].tests[3].params[1].cp = absolute_file;
	tests[9].tests[3].params[2].i = O_RDONLY;
	tests[9].tests[3].params[3].i = 0400;
	tests[9].tests[4].result = 10;		/* hardcoded fd */
	tests[9].tests[4].params[0].i = rel_fd;
	tests[9].tests[4].params[1].cp = newfile;
	tests[9].tests[4].params[2].i = O_RDONLY | O_CREAT;
	tests[9].tests[4].params[3].i = 0666;

	/* readlinkat */
	tests[10].syscall = SYS_readlinkat;
	tests[10].num_of_cases = 3;
	tests[10].name = "readlinkat";
	tests[10].tests[0].result = EBADF;
	tests[10].tests[0].params[0].i = 106;	/* invalid fd */
	tests[10].tests[0].params[1].cp = relative_file;
	tests[10].tests[0].params[2].vp = cbuf;
	tests[10].tests[0].params[3].i = PATH_MAX; 
	tests[10].tests[1].result = ENOTDIR;
	tests[10].tests[1].params[0].i = notd_fd;
	tests[10].tests[1].params[1].cp = relative_file;
	tests[10].tests[1].params[2].vp = cbuf;
	tests[10].tests[1].params[3].i = PATH_MAX; 
	tests[10].tests[2].result = strlen(absolute_file);
	tests[10].tests[2].params[0].i = AT_FDCWD;
	tests[10].tests[2].params[1].cp = symlinkf;
	tests[10].tests[2].params[2].vp = cbuf;
	tests[10].tests[2].params[3].i = PATH_MAX; 

	/* renameat */
	tests[11].syscall = SYS_renameat;
	tests[11].num_of_cases = 5;
	tests[11].name = "renameat";
	tests[11].tests[0].result = EBADF;
	tests[11].tests[0].params[0].i = 106;	/* invalid fd */
	tests[11].tests[0].params[1].cp = file;
	tests[11].tests[0].params[2].i = rel_fd;
	tests[11].tests[0].params[3].cp = file;
	tests[11].tests[1].result = EBADF;
	tests[11].tests[1].params[0].i = rel_fd;
	tests[11].tests[1].params[1].cp = file;
	tests[11].tests[1].params[2].i = 106;	/* invalid fd */
	tests[11].tests[1].params[3].cp = file;
	tests[11].tests[2].result = ENOTDIR;
	tests[11].tests[2].params[0].i = notd_fd;
	tests[11].tests[2].params[1].cp = relative_file;
	tests[11].tests[2].params[2].i = rel_fd;
	tests[11].tests[2].params[3].cp = file;
	tests[11].tests[3].result = ENOTDIR;
	tests[11].tests[3].params[0].i = rel_fd;
	tests[11].tests[3].params[1].cp = file;
	tests[11].tests[3].params[2].i = notd_fd;
	tests[11].tests[3].params[3].cp = relative_file;
	tests[11].tests[4].result = 0;
	tests[11].tests[4].params[0].i = rel_fd;
	tests[11].tests[4].params[1].cp = newfile;
	tests[11].tests[4].params[2].i = AT_FDCWD;
	tests[11].tests[4].params[3].cp = newfile;
	
	/* symlinkat */
	tests[12].syscall = SYS_symlinkat;
	tests[12].num_of_cases = 3;
	tests[12].name = "symlinkat";
	tests[12].tests[0].result = EBADF;
	tests[12].tests[0].params[0].cp = file;
	tests[12].tests[0].params[1].i = 106;	/* invalid fd */
	tests[12].tests[0].params[2].cp = file;
	tests[12].tests[1].result = ENOTDIR;
	tests[12].tests[1].params[0].cp = file;
	tests[12].tests[1].params[1].i = notd_fd;
	tests[12].tests[1].params[2].cp = relative_file;
	tests[12].tests[2].result = 0;
	tests[12].tests[2].params[0].cp = absolute_file;
	tests[12].tests[2].params[1].i = rel_fd;
	tests[12].tests[2].params[2].cp = newslink;


	/* unlinkat */
	tests[13].syscall = SYS_unlinkat;
	tests[13].num_of_cases = 7;
	tests[13].name = "unlinkat";
	tests[13].tests[0].result = EBADF;
	tests[13].tests[0].params[0].i = 106;	/* invalid fd */
	tests[13].tests[0].params[1].cp = relative_file;
	tests[13].tests[0].params[2].i = 0;
	tests[13].tests[1].result = ENOTDIR;
	tests[13].tests[1].params[0].i = notd_fd;
	tests[13].tests[1].params[1].cp = relative_file;
	tests[13].tests[1].params[2].i = 0;
	tests[13].tests[2].result = EINVAL;
	tests[13].tests[2].params[0].i = rel_fd;
	tests[13].tests[2].params[1].cp = file;
	tests[13].tests[2].params[2].i = 123;	/* invalid flag */
	tests[13].tests[3].result = ENOTDIR;
	tests[13].tests[3].params[0].i = rel_fd;
	tests[13].tests[3].params[1].cp = not_dir_path;
	tests[13].tests[3].params[2].i = AT_REMOVEDIR;
	tests[13].tests[4].result = ENOTEMPTY;
	tests[13].tests[4].params[0].i = AT_FDCWD;
	tests[13].tests[4].params[1].cp = relative_path;
	tests[13].tests[4].params[2].i = AT_REMOVEDIR;
	tests[13].tests[5].result = 0;
	tests[13].tests[5].params[0].i = rel_fd;
	tests[13].tests[5].params[1].cp = newdir;
	tests[13].tests[5].params[2].i = AT_REMOVEDIR;
	tests[13].tests[6].result = 0;
	tests[13].tests[6].params[0].i = AT_FDCWD;
	tests[13].tests[6].params[1].cp = newfile;
	tests[13].tests[6].params[2].i = 0;


	/* fexecve */
	tests[14].syscall = SYS_fexecve;
	tests[14].num_of_cases = 2;
	tests[14].name = "fexecve";
	tests[14].tests[0].result = EBADF;
	tests[14].tests[0].params[0].i = 106;	/* invalid fd */
	tests[14].tests[0].params[1].cpp = pargv;
	tests[14].tests[0].params[2].cpp = NULL;
	/* This is EXPECTED to execve /bin/date, so dont expect OK output */
	tests[14].tests[1].result = 0;
	tests[14].tests[1].params[0].i = exec_fd;
	tests[14].tests[1].params[1].cpp = pargv;
	tests[14].tests[1].params[2].cpp = NULL;
}

void
cleanup(void)
{
	system("/bin/sh -c 'rm -rf tmp'");
}

void
setup_once(void)
{
}

int
main(int argc, char *argv[])
{
	int i,j;
	int error;

	(void)argc;
	(void)argv;

	setup();

	for (i = 0; i < NUM_OF_TESTS; i++) {
		printf("\nTest: %s\n", tests[i].name);
		for (j = 0; j < tests[i].num_of_cases; j++) {
			error = syscall(tests[i].syscall,
				tests[i].tests[j].params[0],
				tests[i].tests[j].params[1],
				tests[i].tests[j].params[2],
				tests[i].tests[j].params[3],
				tests[i].tests[j].params[4]);
			if (error == 0) {
				if (tests[i].tests[j].result == 0)
   					printf("#%i ... OK\n", j);
				else {
					printf("#%i ... BAD: ", j);
					printf("expected %i, but got %i\n", tests[i].tests[j].result, error);
				}
			} else 	{
				if (tests[i].tests[j].result == errno)
					printf("#%i ... OK\n", j);
				else {
				   	if (error != tests[i].tests[j].result) {
						printf("#%i ... BAD: ", j);
						printf("expected %i, but got %i\n", tests[i].tests[j].result, error);
					} else 
						printf("#%i ... OK\n", j);
				}
			}
		}
	}

	cleanup();

	return (0);
}
