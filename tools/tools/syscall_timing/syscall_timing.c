/*-
 * Copyright (c) 2003-2004, 2010 Robert N. M. Watson
 * All rights reserved.
 *
 * Portions of this software were developed at the University of Cambridge
 * Computer Laboratory with support from a grant from Google, Inc.
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

#include <sys/param.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/procdesc.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#ifdef WITH_PTHREAD
#include <pthread.h>
#endif
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static struct timespec ts_start, ts_end;
static int alarm_timeout;
static volatile int alarm_fired;

#define	BENCHMARK_FOREACH(I, NUM) for (I = 0; I < NUM && alarm_fired == 0; I++)

static void
alarm_handler(int signum __unused)
{

	alarm_fired = 1;
}

static void
benchmark_start(void)
{
	int error;

	alarm_fired = 0;
	if (alarm_timeout) {
		signal(SIGALRM, alarm_handler);
		alarm(alarm_timeout);
	}
	error = clock_gettime(CLOCK_REALTIME, &ts_start);
	assert(error == 0);
}

static void
benchmark_stop(void)
{
	int error;

	error = clock_gettime(CLOCK_REALTIME, &ts_end);
	assert(error == 0);
}

static uintmax_t
test_access(uintmax_t num, uintmax_t int_arg __unused, const char *path)
{
	uintmax_t i;
	int fd;

	fd = access(path, O_RDONLY);
	if (fd < 0)
		err(-1, "test_access: %s", path);
	close(fd);

	benchmark_start();
	BENCHMARK_FOREACH(i, num) {
		access(path, O_RDONLY);
		close(fd);
	}
	benchmark_stop();
	return (i);
}

static uintmax_t
test_bad_open(uintmax_t num, uintmax_t int_arg __unused, const char *path __unused)
{
	uintmax_t i;

	benchmark_start();
	BENCHMARK_FOREACH(i, num) {
		open("", O_RDONLY);
	}
	benchmark_stop();
	return (i);
}

static uintmax_t
test_chroot(uintmax_t num, uintmax_t int_arg __unused, const char *path __unused)
{
	uintmax_t i;

	if (chroot("/") < 0)
		err(-1, "test_chroot: chroot");
	benchmark_start();
	BENCHMARK_FOREACH(i, num) {
		if (chroot("/") < 0)
			err(-1, "test_chroot: chroot");
	}
	benchmark_stop();
	return (i);
}

static uintmax_t
test_clock_gettime(uintmax_t num, uintmax_t int_arg __unused, const char *path __unused)
{
	struct timespec ts;
	uintmax_t i;

	benchmark_start();
	BENCHMARK_FOREACH(i, num) {
		(void)clock_gettime(CLOCK_REALTIME, &ts);
	}
	benchmark_stop();
	return (i);
}

static uintmax_t
test_create_unlink(uintmax_t num, uintmax_t int_arg __unused, const char *path)
{
	uintmax_t i;
	int fd;

	(void)unlink(path);
	fd = open(path, O_RDWR | O_CREAT, 0600);
	if (fd < 0)
		err(-1, "test_create_unlink: create: %s", path);
	close(fd);
	if (unlink(path) < 0)
		err(-1, "test_create_unlink: unlink: %s", path);
	benchmark_start();
	BENCHMARK_FOREACH(i, num) {
		fd = open(path, O_RDWR | O_CREAT, 0600);
		if (fd < 0)
			err(-1, "test_create_unlink: create: %s", path);
		close(fd);
		if (unlink(path) < 0)
			err(-1, "test_create_unlink: unlink: %s", path);
	}
	benchmark_stop();
	return (i);
}

static uintmax_t
test_fork(uintmax_t num, uintmax_t int_arg __unused, const char *path __unused)
{
	pid_t pid;
	uintmax_t i;

	pid = fork();
	if (pid < 0)
		err(-1, "test_fork: fork");
	if (pid == 0)
		_exit(0);
	if (waitpid(pid, NULL, 0) < 0)
		err(-1, "test_fork: waitpid");
	benchmark_start();
	BENCHMARK_FOREACH(i, num) {
		pid = fork();
		if (pid < 0)
			err(-1, "test_fork: fork");
		if (pid == 0)
			_exit(0);
		if (waitpid(pid, NULL, 0) < 0)
			err(-1, "test_fork: waitpid");
	}
	benchmark_stop();
	return (i);
}

#define	USR_BIN_TRUE	"/usr/bin/true"
static char *execve_args[] = { __DECONST(char *, USR_BIN_TRUE), NULL};
extern char **environ;

static uintmax_t
test_fork_exec(uintmax_t num, uintmax_t int_arg __unused, const char *path __unused)
{
	pid_t pid;
	uintmax_t i;

	pid = fork();
	if (pid < 0)
		err(-1, "test_fork_exec: fork");
	if (pid == 0) {
		(void)execve(USR_BIN_TRUE, execve_args, environ);
		err(-1, "execve");
	}
	if (waitpid(pid, NULL, 0) < 0)
		err(-1, "test_fork: waitpid");
	benchmark_start();
	BENCHMARK_FOREACH(i, num) {
		pid = fork();
		if (pid < 0)
			err(-1, "test_fork_exec: fork");
		if (pid == 0) {
			(void)execve(USR_BIN_TRUE, execve_args, environ);
			err(-1, "test_fork_exec: execve");
		}
		if (waitpid(pid, NULL, 0) < 0)
			err(-1, "test_fork_exec: waitpid");
	}
	benchmark_stop();
	return (i);
}

static uintmax_t
test_getppid(uintmax_t num, uintmax_t int_arg __unused, const char *path __unused)
{
	uintmax_t i;

	/*
	 * This is process-local, but can change, so will require a
	 * lock.
	 */
	benchmark_start();
	BENCHMARK_FOREACH(i, num) {
		getppid();
	}
	benchmark_stop();
	return (i);
}

static uintmax_t
test_getpriority(uintmax_t num, uintmax_t int_arg __unused, const char *path __unused)
{
	uintmax_t i;

	benchmark_start();
	BENCHMARK_FOREACH(i, num) {
		(void)getpriority(PRIO_PROCESS, 0);
	}
	benchmark_stop();
	return (i);
}

/*
 * The point of this one is to figure out the cost of a call into libc,
 * through PLT, and back.
 */
static uintmax_t
test_getprogname(uintmax_t num, uintmax_t int_arg __unused, const char *path __unused)
{
	uintmax_t i;

	benchmark_start();
	BENCHMARK_FOREACH(i, num) {
		(void)getprogname();
	}
	benchmark_stop();
	return (i);
}

static uintmax_t
test_getresuid(uintmax_t num, uintmax_t int_arg __unused, const char *path __unused)
{
	uid_t ruid, euid, suid;
	uintmax_t i;

	benchmark_start();
	BENCHMARK_FOREACH(i, num) {
		(void)getresuid(&ruid, &euid, &suid);
	}
	benchmark_stop();
	return (i);
}

static uintmax_t
test_gettimeofday(uintmax_t num, uintmax_t int_arg __unused, const char *path __unused)
{
	struct timeval tv;
	uintmax_t i;

	benchmark_start();
	BENCHMARK_FOREACH(i, num) {
		(void)gettimeofday(&tv, NULL);
	}
	benchmark_stop();
	return (i);
}

static uintmax_t
test_getuid(uintmax_t num, uintmax_t int_arg __unused, const char *path __unused)
{
	uintmax_t i;

	/*
	 * Thread-local data should require no locking if system
	 * call is MPSAFE.
	 */
	benchmark_start();
	BENCHMARK_FOREACH(i, num) {
		getuid();
	}
	benchmark_stop();
	return (i);
}

static uintmax_t
test_lstat(uintmax_t num, uintmax_t int_arg __unused, const char *path)
{
	struct stat sb;
	uintmax_t i;
	int error;

	benchmark_start();
	BENCHMARK_FOREACH(i, num) {
		error = lstat(path, &sb);
		if (error != 0)
			err(-1, "lstat");
	}
	benchmark_stop();
	return (i);
}

static uintmax_t
test_memcpy(uintmax_t num, uintmax_t int_arg, const char *path __unused)
{
	char buf[int_arg], buf2[int_arg];
	uintmax_t i;

	benchmark_start();
	BENCHMARK_FOREACH(i, num) {
		/*
		 * Copy the memory there and back, to match the total amount
		 * moved by pipeping/pipepingtd tests.
		 */
		memcpy(buf2, buf, int_arg);
		memcpy(buf, buf2, int_arg);
	}
	benchmark_stop();

	return (i);
}

static uintmax_t
test_open_close(uintmax_t num, uintmax_t int_arg __unused, const char *path)
{
	uintmax_t i;
	int fd;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		err(-1, "test_open_close: %s", path);
	close(fd);

	benchmark_start();
	BENCHMARK_FOREACH(i, num) {
		fd = open(path, O_RDONLY);
		if (fd < 0)
			err(-1, "test_open_close: %s", path);
		close(fd);
	}
	benchmark_stop();
	return (i);
}

static uintmax_t
test_open_read_close(uintmax_t num, uintmax_t int_arg, const char *path)
{
	char buf[int_arg];
	uintmax_t i;
	int fd;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		err(-1, "test_open_read_close: %s", path);
	(void)read(fd, buf, int_arg);
	close(fd);

	benchmark_start();
	BENCHMARK_FOREACH(i, num) {
		fd = open(path, O_RDONLY);
		if (fd < 0)
			err(-1, "test_open_read_close: %s", path);
		(void)read(fd, buf, int_arg);
		close(fd);
	}
	benchmark_stop();
	return (i);
}

static uintmax_t
test_pipe(uintmax_t num, uintmax_t int_arg __unused, const char *path __unused)
{
	int fd[2];
	uintmax_t i;

	/*
	 * pipe creation is expensive, as it will allocate a new file
	 * descriptor, allocate a new pipe, hook it all up, and return.
	 * Destroying is also expensive, as we now have to free up
	 * the file descriptors and return the pipe.
	 */
	if (pipe(fd) < 0)
		err(-1, "test_pipe: pipe");
	close(fd[0]);
	close(fd[1]);
	benchmark_start();
	BENCHMARK_FOREACH(i, num) {
		if (pipe(fd) == -1)
			err(-1, "test_pipe: pipe");
		close(fd[0]);
		close(fd[1]);
	}
	benchmark_stop();
	return (i);
}

static void
readx(int fd, char *buf, size_t size)
{
	ssize_t ret;

	do {
		ret = read(fd, buf, size);
		if (ret == -1)
			err(1, "read");
		assert((size_t)ret <= size);
		size -= ret;
		buf += ret;
	} while (size > 0);
}

static void
writex(int fd, const char *buf, size_t size)
{
	ssize_t ret;

	do {
		ret = write(fd, buf, size);
		if (ret == -1)
			err(1, "write");
		assert((size_t)ret <= size);
		size -= ret;
		buf += ret;
	} while (size > 0);
}

static uintmax_t
test_pipeping(uintmax_t num, uintmax_t int_arg, const char *path __unused)
{
	char buf[int_arg];
	uintmax_t i;
	pid_t pid;
	int fd[2], procfd;

	if (pipe(fd) < 0)
		err(-1, "pipe");

	pid = pdfork(&procfd, 0);
	if (pid < 0)
		err(1, "pdfork");

	if (pid == 0) {
		close(fd[0]);

		for (;;) {
			readx(fd[1], buf, int_arg);
			writex(fd[1], buf, int_arg);
		}
	}

	close(fd[1]);

	benchmark_start();
	BENCHMARK_FOREACH(i, num) {
		writex(fd[0], buf, int_arg);
		readx(fd[0], buf, int_arg);
	}
	benchmark_stop();

	close(procfd);
	return (i);
}

#ifdef WITH_PTHREAD
struct pipepingtd_ctx {
	int		fd;
	uintmax_t	int_arg;
};

static void *
pipepingtd_proc(void *arg)
{
	struct pipepingtd_ctx *ctxp;
	int fd;
	void *buf;
	uintmax_t int_arg;

	ctxp = arg;
	fd = ctxp->fd;
	int_arg = ctxp->int_arg;

	buf = malloc(int_arg);
	if (buf == NULL)
		err(1, "malloc");

	for (;;) {
		readx(fd, buf, int_arg);
		writex(fd, buf, int_arg);
	}
}

static uintmax_t
test_pipepingtd(uintmax_t num, uintmax_t int_arg, const char *path __unused)
{
	struct pipepingtd_ctx ctx;
	char buf[int_arg];
	pthread_t td;
	uintmax_t i;
	int error, fd[2];

	if (pipe(fd) < 0)
		err(-1, "pipe");

	ctx.fd = fd[1];
	ctx.int_arg = int_arg;

	error = pthread_create(&td, NULL, pipepingtd_proc, &ctx);
	if (error != 0)
		err(1, "pthread_create");

	benchmark_start();
	BENCHMARK_FOREACH(i, num) {
		writex(fd[0], buf, int_arg);
		readx(fd[0], buf, int_arg);
	}
	benchmark_stop();
	pthread_cancel(td);

	return (i);
}
#endif /* WITH_PTHREAD */

static uintmax_t
test_read(uintmax_t num, uintmax_t int_arg, const char *path)
{
	char buf[int_arg];
	uintmax_t i;
	int fd;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		err(-1, "test_open_read: %s", path);
	(void)pread(fd, buf, int_arg, 0);

	benchmark_start();
	BENCHMARK_FOREACH(i, num) {
		(void)pread(fd, buf, int_arg, 0);
	}
	benchmark_stop();
	close(fd);
	return (i);
}

static uintmax_t
test_select(uintmax_t num, uintmax_t int_arg __unused, const char *path __unused)
{
	fd_set readfds, writefds, exceptfds;
	struct timeval tv;
	uintmax_t i;

	FD_ZERO(&readfds);
	FD_ZERO(&writefds);
	FD_ZERO(&exceptfds);

	tv.tv_sec = 0;
	tv.tv_usec = 0;

	benchmark_start();
	BENCHMARK_FOREACH(i, num) {
		(void)select(0, &readfds, &writefds, &exceptfds, &tv);
	}
	benchmark_stop();
	return (i);
}

static uintmax_t
test_semaping(uintmax_t num, uintmax_t int_arg __unused, const char *path __unused)
{
	uintmax_t i;
	pid_t pid;
	sem_t *buf;
	int error, j, procfd;

	buf = mmap(0, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_ANON | MAP_SHARED, -1, 0);
	if (buf == MAP_FAILED)
		err(1, "mmap");

	for (j = 0; j < 2; j++) {
		error = sem_init(&buf[j], 1, 0);
		if (error != 0)
			err(1, "sem_init");
	}

	pid = pdfork(&procfd, 0);
	if (pid < 0)
		err(1, "pdfork");

	if (pid == 0) {
		for (;;) {
			error = sem_wait(&buf[0]);
			if (error != 0)
				err(1, "sem_wait");
			error = sem_post(&buf[1]);
			if (error != 0)
				err(1, "sem_post");
		}
	}

	benchmark_start();
	BENCHMARK_FOREACH(i, num) {
		error = sem_post(&buf[0]);
		if (error != 0)
			err(1, "sem_post");
		error = sem_wait(&buf[1]);
		if (error != 0)
			err(1, "sem_wait");
	}
	benchmark_stop();

	close(procfd);

	for (j = 0; j < 2; j++) {
		error = sem_destroy(&buf[j]);
		if (error != 0)
			err(1, "sem_destroy");
	}

	error = munmap(buf, PAGE_SIZE);
	if (error != 0)
		err(1, "munmap");

	return (i);
}

static uintmax_t
test_setuid(uintmax_t num, uintmax_t int_arg __unused, const char *path __unused)
{
	uid_t uid;
	uintmax_t i;

	uid = getuid();
	if (setuid(uid) < 0)
		err(-1, "test_setuid: setuid");
	benchmark_start();
	BENCHMARK_FOREACH(i, num) {
		if (setuid(uid) < 0)
			err(-1, "test_setuid: setuid");
	}
	benchmark_stop();
	return (i);
}

static uintmax_t
test_shmfd(uintmax_t num, uintmax_t int_arg __unused, const char *path __unused)
{
	uintmax_t i;
	int shmfd;

	shmfd = shm_open(SHM_ANON, O_CREAT | O_RDWR, 0600);
	if (shmfd < 0)
		err(-1, "test_shmfd: shm_open");
	close(shmfd);
	benchmark_start();
	BENCHMARK_FOREACH(i, num) {
		shmfd = shm_open(SHM_ANON, O_CREAT | O_RDWR, 0600);
		if (shmfd < 0)
			err(-1, "test_shmfd: shm_open");
		close(shmfd);
	}
	benchmark_stop();
	return (i);
}

static uintmax_t
test_shmfd_dup(uintmax_t num, uintmax_t int_arg __unused, const char *path __unused)
{
	uintmax_t i;
	int fd, shmfd;

	shmfd = shm_open(SHM_ANON, O_CREAT | O_RDWR, 0600);
	if (shmfd < 0)
		err(-1, "test_shmfd_dup: shm_open");
	fd = dup(shmfd);
	if (fd >= 0)
		close(fd);
	benchmark_start();
	BENCHMARK_FOREACH(i, num) {
		fd = dup(shmfd);
		if (fd >= 0)
			close(fd);
	}
	benchmark_stop();
	close(shmfd);
	return (i);
}

static uintmax_t
test_shmfd_fstat(uintmax_t num, uintmax_t int_arg __unused, const char *path __unused)
{
	struct stat sb;
	uintmax_t i;
	int shmfd;

	shmfd = shm_open(SHM_ANON, O_CREAT | O_RDWR, 0600);
	if (shmfd < 0)
		err(-1, "test_shmfd_fstat: shm_open");
	if (fstat(shmfd, &sb) < 0)
		err(-1, "test_shmfd_fstat: fstat");
	benchmark_start();
	BENCHMARK_FOREACH(i, num) {
		(void)fstat(shmfd, &sb);
	}
	benchmark_stop();
	close(shmfd);
	return (i);
}

static uintmax_t
test_socket_stream(uintmax_t num, uintmax_t int_arg, const char *path __unused)
{
	uintmax_t i;
	int so;

	so = socket(int_arg, SOCK_STREAM, 0);
	if (so < 0)
		err(-1, "test_socket_stream: socket");
	close(so);
	benchmark_start();
	BENCHMARK_FOREACH(i, num) {
		so = socket(int_arg, SOCK_STREAM, 0);
		if (so == -1)
			err(-1, "test_socket_stream: socket");
		close(so);
	}
	benchmark_stop();
	return (i);
}

static uintmax_t
test_socket_dgram(uintmax_t num, uintmax_t int_arg, const char *path __unused)
{
	uintmax_t i;
	int so;

	so = socket(int_arg, SOCK_DGRAM, 0);
	if (so < 0)
		err(-1, "test_socket_dgram: socket");
	close(so);
	benchmark_start();
	BENCHMARK_FOREACH(i, num) {
		so = socket(int_arg, SOCK_DGRAM, 0);
		if (so == -1)
			err(-1, "test_socket_dgram: socket");
		close(so);
	}
	benchmark_stop();
	return (i);
}

static uintmax_t
test_socketpair_stream(uintmax_t num, uintmax_t int_arg __unused, const char *path __unused)
{
	uintmax_t i;
	int so[2];

	if (socketpair(PF_LOCAL, SOCK_STREAM, 0, so) == -1)
		err(-1, "test_socketpair_stream: socketpair");
	close(so[0]);
	close(so[1]);
	benchmark_start();
	BENCHMARK_FOREACH(i, num) {
		if (socketpair(PF_LOCAL, SOCK_STREAM, 0, so) == -1)
			err(-1, "test_socketpair_stream: socketpair");
		close(so[0]);
		close(so[1]);
	}
	benchmark_stop();
	return (i);
}

static uintmax_t
test_socketpair_dgram(uintmax_t num, uintmax_t int_arg __unused, const char *path __unused)
{
	uintmax_t i;
	int so[2];

	if (socketpair(PF_LOCAL, SOCK_DGRAM, 0, so) == -1)
		err(-1, "test_socketpair_dgram: socketpair");
	close(so[0]);
	close(so[1]);
	benchmark_start();
	BENCHMARK_FOREACH(i, num) {
		if (socketpair(PF_LOCAL, SOCK_DGRAM, 0, so) == -1)
			err(-1, "test_socketpair_dgram: socketpair");
		close(so[0]);
		close(so[1]);
	}
	benchmark_stop();
	return (i);
}

static uintmax_t
test_readlink(uintmax_t num, uintmax_t int_arg __unused, const char *path)
{
	char buf[PATH_MAX];
	ssize_t rv;
	uintmax_t i;

	benchmark_start();
	BENCHMARK_FOREACH(i, num) {
		rv = readlink(path, buf, sizeof(buf));
		if (rv < 0 && errno != EINVAL)
			err(-1, "readlink");
	}
	benchmark_stop();
	return (i);
}

static uintmax_t
test_vfork(uintmax_t num, uintmax_t int_arg __unused, const char *path __unused)
{
	pid_t pid;
	uintmax_t i;

	pid = vfork();
	if (pid < 0)
		err(-1, "test_vfork: vfork");
	if (pid == 0)
		_exit(0);
	if (waitpid(pid, NULL, 0) < 0)
		err(-1, "test_vfork: waitpid");
	benchmark_start();
	BENCHMARK_FOREACH(i, num) {
		pid = vfork();
		if (pid < 0)
			err(-1, "test_vfork: vfork");
		if (pid == 0)
			_exit(0);
		if (waitpid(pid, NULL, 0) < 0)
			err(-1, "test_vfork: waitpid");
	}
	benchmark_stop();
	return (i);
}

static uintmax_t
test_vfork_exec(uintmax_t num, uintmax_t int_arg __unused, const char *path __unused)
{
	pid_t pid;
	uintmax_t i;

	pid = vfork();
	if (pid < 0)
		err(-1, "test_vfork_exec: vfork");
	if (pid == 0) {
		(void)execve(USR_BIN_TRUE, execve_args, environ);
		err(-1, "test_vfork_exec: execve");
	}
	if (waitpid(pid, NULL, 0) < 0)
		err(-1, "test_vfork_exec: waitpid");
	benchmark_start();
	BENCHMARK_FOREACH(i, num) {
		pid = vfork();
		if (pid < 0)
			err(-1, "test_vfork_exec: vfork");
		if (pid == 0) {
			(void)execve(USR_BIN_TRUE, execve_args, environ);
			err(-1, "execve");
		}
		if (waitpid(pid, NULL, 0) < 0)
			err(-1, "test_vfork_exec: waitpid");
	}
	benchmark_stop();
	return (i);
}

struct test {
	const char	*t_name;
	uintmax_t	(*t_func)(uintmax_t, uintmax_t, const char *);
	int		 t_flags;
	uintmax_t	 t_int;
};

#define	FLAG_PATH	0x00000001

static const struct test tests[] = {
	{ "access", test_access, .t_flags = FLAG_PATH },
	{ "bad_open", test_bad_open, .t_flags = 0 },
	{ "chroot", test_chroot, .t_flags = 0 },
	{ "clock_gettime", test_clock_gettime, .t_flags = 0 },
	{ "create_unlink", test_create_unlink, .t_flags = FLAG_PATH },
	{ "fork", test_fork, .t_flags = 0 },
	{ "fork_exec", test_fork_exec, .t_flags = 0 },
	{ "getppid", test_getppid, .t_flags = 0 },
	{ "getpriority", test_getpriority, .t_flags = 0 },
	{ "getprogname", test_getprogname, .t_flags = 0 },
	{ "getresuid", test_getresuid, .t_flags = 0 },
	{ "gettimeofday", test_gettimeofday, .t_flags = 0 },
	{ "getuid", test_getuid, .t_flags = 0 },
	{ "lstat", test_lstat, .t_flags = FLAG_PATH },
	{ "memcpy_1", test_memcpy, .t_flags = 0, .t_int = 1 },
	{ "memcpy_10", test_memcpy, .t_flags = 0, .t_int = 10 },
	{ "memcpy_100", test_memcpy, .t_flags = 0, .t_int = 100 },
	{ "memcpy_1000", test_memcpy, .t_flags = 0, .t_int = 1000 },
	{ "memcpy_10000", test_memcpy, .t_flags = 0, .t_int = 10000 },
	{ "memcpy_100000", test_memcpy, .t_flags = 0, .t_int = 100000 },
	{ "memcpy_1000000", test_memcpy, .t_flags = 0, .t_int = 1000000 },
	{ "open_close", test_open_close, .t_flags = FLAG_PATH },
	{ "open_read_close_1", test_open_read_close, .t_flags = FLAG_PATH,
	    .t_int = 1 },
	{ "open_read_close_10", test_open_read_close, .t_flags = FLAG_PATH,
	    .t_int = 10 },
	{ "open_read_close_100", test_open_read_close, .t_flags = FLAG_PATH,
	    .t_int = 100 },
	{ "open_read_close_1000", test_open_read_close, .t_flags = FLAG_PATH,
	    .t_int = 1000 },
	{ "open_read_close_10000", test_open_read_close,
	    .t_flags = FLAG_PATH, .t_int = 10000 },
	{ "open_read_close_100000", test_open_read_close,
	    .t_flags = FLAG_PATH, .t_int = 100000 },
	{ "open_read_close_1000000", test_open_read_close,
	    .t_flags = FLAG_PATH, .t_int = 1000000 },
	{ "pipe", test_pipe, .t_flags = 0 },
	{ "pipeping_1", test_pipeping, .t_flags = 0, .t_int = 1 },
	{ "pipeping_10", test_pipeping, .t_flags = 0, .t_int = 10 },
	{ "pipeping_100", test_pipeping, .t_flags = 0, .t_int = 100 },
	{ "pipeping_1000", test_pipeping, .t_flags = 0, .t_int = 1000 },
	{ "pipeping_10000", test_pipeping, .t_flags = 0, .t_int = 10000 },
	{ "pipeping_100000", test_pipeping, .t_flags = 0, .t_int = 100000 },
	{ "pipeping_1000000", test_pipeping, .t_flags = 0, .t_int = 1000000 },
#ifdef WITH_PTHREAD
	{ "pipepingtd_1", test_pipepingtd, .t_flags = 0, .t_int = 1 },
	{ "pipepingtd_10", test_pipepingtd, .t_flags = 0, .t_int = 10 },
	{ "pipepingtd_100", test_pipepingtd, .t_flags = 0, .t_int = 100 },
	{ "pipepingtd_1000", test_pipepingtd, .t_flags = 0, .t_int = 1000 },
	{ "pipepingtd_10000", test_pipepingtd, .t_flags = 0, .t_int = 10000 },
	{ "pipepingtd_100000", test_pipepingtd, .t_flags = 0, .t_int = 100000 },
	{ "pipepingtd_1000000", test_pipepingtd, .t_flags = 0, .t_int = 1000000 },
#endif
	{ "read_1", test_read, .t_flags = FLAG_PATH, .t_int = 1 },
	{ "read_10", test_read, .t_flags = FLAG_PATH, .t_int = 10 },
	{ "read_100", test_read, .t_flags = FLAG_PATH, .t_int = 100 },
	{ "read_1000", test_read, .t_flags = FLAG_PATH, .t_int = 1000 },
	{ "read_10000", test_read, .t_flags = FLAG_PATH, .t_int = 10000 },
	{ "read_100000", test_read, .t_flags = FLAG_PATH, .t_int = 100000 },
	{ "read_1000000", test_read, .t_flags = FLAG_PATH, .t_int = 1000000 },
	{ "select", test_select, .t_flags = 0 },
	{ "semaping", test_semaping, .t_flags = 0 },
	{ "setuid", test_setuid, .t_flags = 0 },
	{ "shmfd", test_shmfd, .t_flags = 0 },
	{ "shmfd_dup", test_shmfd_dup, .t_flags = 0 },
	{ "shmfd_fstat", test_shmfd_fstat, .t_flags = 0 },
	{ "socket_local_stream", test_socket_stream, .t_int = PF_LOCAL },
	{ "socket_local_dgram", test_socket_dgram, .t_int = PF_LOCAL },
	{ "socketpair_stream", test_socketpair_stream, .t_flags = 0 },
	{ "socketpair_dgram", test_socketpair_dgram, .t_flags = 0 },
	{ "socket_tcp", test_socket_stream, .t_int = PF_INET },
	{ "socket_udp", test_socket_dgram, .t_int = PF_INET },
	{ "readlink", test_readlink, .t_flags = FLAG_PATH },
	{ "vfork", test_vfork, .t_flags = 0 },
	{ "vfork_exec", test_vfork_exec, .t_flags = 0 },
};
static const int tests_count = sizeof(tests) / sizeof(tests[0]);

static void
usage(void)
{
	int i;

	fprintf(stderr, "syscall_timing [-i iterations] [-l loops] "
	    "[-p path] [-s seconds] test\n");
	for (i = 0; i < tests_count; i++)
		fprintf(stderr, "  %s\n", tests[i].t_name);
	exit(-1);
}

int
main(int argc, char *argv[])
{
	struct timespec ts_res;
	const struct test *the_test;
	const char *path;
	char *tmp_dir, *tmp_path;
	long long ll;
	char *endp;
	int ch, fd, error, i, j, rv;
	uintmax_t iterations, k, loops;

	alarm_timeout = 1;
	iterations = 0;
	loops = 10;
	path = NULL;
	tmp_path = NULL;
	while ((ch = getopt(argc, argv, "i:l:p:s:")) != -1) {
		switch (ch) {
		case 'i':
			ll = strtol(optarg, &endp, 10);
			if (*endp != 0 || ll < 1)
				usage();
			iterations = ll;
			break;

		case 'l':
			ll = strtol(optarg, &endp, 10);
			if (*endp != 0 || ll < 1 || ll > 100000)
				usage();
			loops = ll;
			break;

		case 'p':
			path = optarg;
			break;

		case 's':
			ll = strtol(optarg, &endp, 10);
			if (*endp != 0 || ll < 1 || ll > 60*60)
				usage();
			alarm_timeout = ll;
			break;

		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (iterations < 1 && alarm_timeout < 1)
		usage();
	if (iterations < 1)
		iterations = UINT64_MAX;
	if (loops < 1)
		loops = 1;

	if (argc < 1)
		usage();

	/*
	 * Validate test list and that, if a path is required, it is
	 * defined.
	 */
	for (j = 0; j < argc; j++) {
		the_test = NULL;
		for (i = 0; i < tests_count; i++) {
			if (strcmp(argv[j], tests[i].t_name) == 0)
				the_test = &tests[i];
		}
		if (the_test == NULL)
			usage();
		if ((the_test->t_flags & FLAG_PATH) && (path == NULL)) {
			tmp_dir = strdup("/tmp/syscall_timing.XXXXXXXX");
			if (tmp_dir == NULL)
				err(1, "strdup");
			tmp_dir = mkdtemp(tmp_dir);
			if (tmp_dir == NULL)
				err(1, "mkdtemp");
			rv = asprintf(&tmp_path, "%s/testfile", tmp_dir);
			if (rv <= 0)
				err(1, "asprintf");
		}
	}

	error = clock_getres(CLOCK_REALTIME, &ts_res);
	assert(error == 0);
	printf("Clock resolution: %ju.%09ju\n", (uintmax_t)ts_res.tv_sec,
	    (uintmax_t)ts_res.tv_nsec);
	printf("test\tloop\ttime\titerations\tperiteration\n");

	for (j = 0; j < argc; j++) {
		uintmax_t calls, nsecsperit;

		the_test = NULL;
		for (i = 0; i < tests_count; i++) {
			if (strcmp(argv[j], tests[i].t_name) == 0)
				the_test = &tests[i];
		}

		if (tmp_path != NULL) {
			fd = open(tmp_path, O_WRONLY | O_CREAT, 0700);
			if (fd < 0)
				err(1, "cannot open %s", tmp_path);
			error = ftruncate(fd, 1000000);
			if (error != 0)
				err(1, "ftruncate");
			error = close(fd);
			if (error != 0)
				err(1, "close");
			path = tmp_path;
		}

		/*
		 * Run one warmup, then do the real thing (loops) times.
		 */
		the_test->t_func(iterations, the_test->t_int, path);
		calls = 0;
		for (k = 0; k < loops; k++) {
			calls = the_test->t_func(iterations, the_test->t_int,
			    path);
			timespecsub(&ts_end, &ts_start, &ts_end);
			printf("%s\t%ju\t", the_test->t_name, k);
			printf("%ju.%09ju\t%ju\t", (uintmax_t)ts_end.tv_sec,
			    (uintmax_t)ts_end.tv_nsec, calls);

		/*
		 * Note.  This assumes that each iteration takes less than
		 * a second, and that our total nanoseconds doesn't exceed
		 * the room in our arithmetic unit.  Fine for system calls,
		 * but not for long things.
		 */
			nsecsperit = ts_end.tv_sec * 1000000000;
			nsecsperit += ts_end.tv_nsec;
			nsecsperit /= calls;
			printf("0.%09ju\n", (uintmax_t)nsecsperit);
		}
	}

	if (tmp_path != NULL) {
		error = unlink(tmp_path);
		if (error != 0 && errno != ENOENT)
			warn("cannot unlink %s", tmp_path);
		error = rmdir(tmp_dir);
		if (error != 0)
			warn("cannot rmdir %s", tmp_dir);
	}

	return (0);
}
