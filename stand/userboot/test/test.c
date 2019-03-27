/*-
 * Copyright (c) 2011 Google, Inc.
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
#include <sys/disk.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <dlfcn.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include <userboot.h>

char *host_base = NULL;
struct termios term, oldterm;
char *image;
size_t image_size;
int disk_fd = -1;

uint64_t regs[16];
uint64_t pc;

void test_exit(void *arg, int v);

/*
 * Console i/o
 */

void
test_putc(void *arg, int ch)
{
	char c = ch;

	write(1, &c, 1);
}

int
test_getc(void *arg)
{
	char c;

	if (read(0, &c, 1) == 1)
		return c;
	return -1;
}

int
test_poll(void *arg)
{
	int n;

	if (ioctl(0, FIONREAD, &n) >= 0)
		return (n > 0);
	return (0);
}

/*
 * Host filesystem i/o
 */

struct test_file {
	int tf_isdir;
	size_t tf_size;
	struct stat tf_stat;
	union {
		int fd;
		DIR *dir;
	} tf_u;
};

int
test_open(void *arg, const char *filename, void **h_return)
{
	struct stat st;
	struct test_file *tf;
	char path[PATH_MAX];

	if (!host_base)
		return (ENOENT);

	strlcpy(path, host_base, PATH_MAX);
	if (path[strlen(path) - 1] == '/')
		path[strlen(path) - 1] = 0;
	strlcat(path, filename, PATH_MAX);
	tf = malloc(sizeof(struct test_file));
	if (stat(path, &tf->tf_stat) < 0) {
		free(tf);
		return (errno);
	}

	tf->tf_size = st.st_size;
	if (S_ISDIR(tf->tf_stat.st_mode)) {
		tf->tf_isdir = 1;
		tf->tf_u.dir = opendir(path);
		if (!tf->tf_u.dir)
			goto out;
                *h_return = tf;
		return (0);
	}
	if (S_ISREG(tf->tf_stat.st_mode)) {
		tf->tf_isdir = 0;
		tf->tf_u.fd = open(path, O_RDONLY);
		if (tf->tf_u.fd < 0)
			goto out;
                *h_return = tf;
		return (0);
	}

out:
	free(tf);
	return (EINVAL);
}

int
test_close(void *arg, void *h)
{
	struct test_file *tf = h;

	if (tf->tf_isdir)
		closedir(tf->tf_u.dir);
	else
		close(tf->tf_u.fd);
	free(tf);

	return (0);
}

int
test_isdir(void *arg, void *h)
{
	struct test_file *tf = h;

	return (tf->tf_isdir);
}

int
test_read(void *arg, void *h, void *dst, size_t size, size_t *resid_return)
{
	struct test_file *tf = h;
	ssize_t sz;

	if (tf->tf_isdir)
		return (EINVAL);
	sz = read(tf->tf_u.fd, dst, size);
	if (sz < 0)
		return (EINVAL);
	*resid_return = size - sz;
	return (0);
}

int
test_readdir(void *arg, void *h, uint32_t *fileno_return, uint8_t *type_return,
    size_t *namelen_return, char *name)
{
	struct test_file *tf = h;
	struct dirent *dp;

	if (!tf->tf_isdir)
		return (EINVAL);

	dp = readdir(tf->tf_u.dir);
	if (!dp)
		return (ENOENT);

	/*
	 * Note: d_namlen is in the range 0..255 and therefore less
	 * than PATH_MAX so we don't need to test before copying.
	 */
	*fileno_return = dp->d_fileno;
	*type_return = dp->d_type;
	*namelen_return = dp->d_namlen;
	memcpy(name, dp->d_name, dp->d_namlen);
	name[dp->d_namlen] = 0;

	return (0);
}

int
test_seek(void *arg, void *h, uint64_t offset, int whence)
{
	struct test_file *tf = h;

	if (tf->tf_isdir)
		return (EINVAL);
	if (lseek(tf->tf_u.fd, offset, whence) < 0)
		return (errno);
	return (0);
}

int
test_stat(void *arg, void *h, int *mode_return, int *uid_return, int *gid_return,
    uint64_t *size_return)
{
	struct test_file *tf = h;

	*mode_return = tf->tf_stat.st_mode;
	*uid_return = tf->tf_stat.st_uid;
	*gid_return = tf->tf_stat.st_gid;
	*size_return = tf->tf_stat.st_size;
	return (0);
}

/*
 * Disk image i/o
 */

int
test_diskread(void *arg, int unit, uint64_t offset, void *dst, size_t size,
    size_t *resid_return)
{
	ssize_t n;

	if (unit != 0 || disk_fd == -1)
		return (EIO);
	n = pread(disk_fd, dst, size, offset);
	if (n < 0)
		return (errno);
	*resid_return = size - n;
	return (0);
}

int
test_diskioctl(void *arg, int unit, u_long cmd, void *data)
{
	struct stat sb;

	if (unit != 0 || disk_fd == -1)
		return (EBADF);
	switch (cmd) {
	case DIOCGSECTORSIZE:
		*(u_int *)data = 512;
		break;
	case DIOCGMEDIASIZE:
		if (fstat(disk_fd, &sb) == 0)
			*(off_t *)data = sb.st_size;
		else
			return (ENOTTY);
		break;
	default:
		return (ENOTTY);
	}
	return (0);
}

/*
 * Guest virtual machine i/o
 *
 * Note: guest addresses are kernel virtual
 */

int
test_copyin(void *arg, const void *from, uint64_t to, size_t size)
{

	to &= 0x7fffffff;
	if (to > image_size)
		return (EFAULT);
	if (to + size > image_size)
		size = image_size - to;
	memcpy(&image[to], from, size);
	return(0);
}

int
test_copyout(void *arg, uint64_t from, void *to, size_t size)
{

	from &= 0x7fffffff;
	if (from > image_size)
		return (EFAULT);
	if (from + size > image_size)
		size = image_size - from;
	memcpy(to, &image[from], size);
	return(0);
}

void
test_setreg(void *arg, int r, uint64_t v)
{

	if (r < 0 || r >= 16)
		return;
	regs[r] = v;
}

void
test_setmsr(void *arg, int r, uint64_t v)
{
}

void
test_setcr(void *arg, int r, uint64_t v)
{
}

void
test_setgdt(void *arg, uint64_t v, size_t sz)
{
}

void
test_exec(void *arg, uint64_t pc)
{
	printf("Execute at 0x%"PRIu64"\n", pc);
	test_exit(arg, 0);
}

/*
 * Misc
 */

void
test_delay(void *arg, int usec)
{

	usleep(usec);
}

void
test_exit(void *arg, int v)
{

	tcsetattr(0, TCSAFLUSH, &oldterm);
	exit(v);
}

void
test_getmem(void *arg, uint64_t *lowmem, uint64_t *highmem)
{

        *lowmem = 128*1024*1024;
        *highmem = 0;
}

char *
test_getenv(void *arg, int idx)
{
	static char *vars[] = {
		"foo=bar",
		"bar=barbar",
		NULL
	};

	return (vars[idx]);
}

struct loader_callbacks cb = {
	.putc = test_putc,
	.getc = test_getc,
	.poll = test_poll,

	.open = test_open,
	.close = test_close,
	.isdir = test_isdir,
	.read = test_read,
	.readdir = test_readdir,
	.seek = test_seek,
	.stat = test_stat,

	.diskread = test_diskread,
	.diskioctl = test_diskioctl,

	.copyin = test_copyin,
	.copyout = test_copyout,
	.setreg = test_setreg,
	.setmsr = test_setmsr,
	.setcr = test_setcr,
        .setgdt = test_setgdt,
	.exec = test_exec,

	.delay = test_delay,
	.exit = test_exit,
        .getmem = test_getmem,

	.getenv = test_getenv,
};

void
usage()
{

	printf("usage: [-b <userboot shared object>] [-d <disk image path>] [-h <host filesystem path>\n");
	exit(1);
}

int
main(int argc, char** argv)
{
	void *h;
	void (*func)(struct loader_callbacks *, void *, int, int) __dead2;
	int opt;
	char *disk_image = NULL;
	const char *userboot_obj = "/boot/userboot.so";

	while ((opt = getopt(argc, argv, "b:d:h:")) != -1) {
		switch (opt) {
		case 'b':
			userboot_obj = optarg;
			break;

		case 'd':
			disk_image = optarg;
			break;

		case 'h':
			host_base = optarg;
			break;

		case '?':
			usage();
		}
	}

	h = dlopen(userboot_obj, RTLD_LOCAL);
	if (!h) {
		printf("%s\n", dlerror());
		return (1);
	}
	func = dlsym(h, "loader_main");
	if (!func) {
		printf("%s\n", dlerror());
		return (1);
	}

	image_size = 128*1024*1024;
	image = malloc(image_size);
	if (disk_image) {
		disk_fd = open(disk_image, O_RDONLY);
		if (disk_fd < 0)
			err(1, "Can't open disk image '%s'", disk_image);
	}

	tcgetattr(0, &term);
	oldterm = term;
	term.c_iflag &= ~(ICRNL);
	term.c_lflag &= ~(ICANON|ECHO);
	tcsetattr(0, TCSAFLUSH, &term);

	func(&cb, NULL, USERBOOT_VERSION_3, disk_fd >= 0);
}
