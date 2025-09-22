/*	$OpenBSD: futex.c,v 1.5 2021/11/22 18:42:16 kettenis Exp $ */
/*
 * Copyright (c) 2017 Martin Pieuchot
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/time.h>
#include <sys/futex.h>
#include <sys/mman.h>
#include <sys/wait.h>

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "futex.h"

uint32_t lock = 0;
uint32_t *shlock;

void handler(int);
void *signaled(void *);
void *awakener(void *);

int
main(int argc, char *argv[])
{
	char filename[] = "/tmp/futex.XXXXXXXX";
	struct sigaction sa;
	struct timespec tmo = { 0, 5000 };
	pthread_t thread;
	pid_t pid;
	int fd, i, status;

	/* Invalid operation */
	assert(futex(&lock, 0xFFFF, 0, 0, NULL) == -1);
	assert(errno == ENOSYS);

	/* Incorrect pointer */
	assert(futex_twait((void *)0xdeadbeef, 1, 0, NULL, 0) == -1);
	assert(errno == EFAULT);

	/* If (lock != 1) return EAGAIN */
	assert(futex_twait(&lock, 1, 0, NULL, 0) == -1);
	assert(errno == EAGAIN);

	/* Deadlock for 5000ns */
	assert(futex_twait(&lock, 0, CLOCK_REALTIME, &tmo, 0) == -1);
	assert(errno == ETIMEDOUT);

	/* Interrupt a thread waiting on a futex. */
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = handler;
	assert(sigaction(SIGUSR1, &sa, NULL) == 0);
	assert(pthread_create(&thread, NULL, signaled, NULL) == 0);
	usleep(100);
	assert(pthread_kill(thread, SIGUSR1) == 0);
	assert(pthread_join(thread, NULL) == 0);

	/* Wait until another thread awakes us. */
	assert(pthread_create(&thread, NULL, awakener, NULL) == 0);
	assert(futex_twait(&lock, 0, 0, NULL, 0) == 0);
	assert(pthread_join(thread, NULL) == 0);

	/* Create a uvm object for sharing a lock. */
	fd = mkstemp(filename);
	assert(fd != -1);
	unlink(filename);
	assert(ftruncate(fd, 65536) == 0);
	shlock = mmap(NULL, sizeof(*shlock), PROT_READ | PROT_WRITE,
	    MAP_SHARED, fd, 0);
	assert(shlock != MAP_FAILED);
	close(fd);

	/* Wake another process. */
	pid = fork();
	assert(pid != -1);
	if (pid == 0) {
		usleep(50000);
		futex_wake(shlock, -1, 0);
		_exit(0);
	} else {
		assert(futex_twait(shlock, 0, 0, NULL, 0) == 0);
		assert(waitpid(pid, &status, 0) == pid);
		assert(WIFEXITED(status));
		assert(WEXITSTATUS(status) == 0);
	}

	/* Cannot wake another process using a private futex. */
	for (i = 1; i < 4; i++) {
		pid = fork();
		assert(pid != -1);
		if (pid == 0) {
			usleep(50000);
			futex_wake(shlock, -1,
			    (i & 1) ? FUTEX_PRIVATE_FLAG : 0);
			_exit(0);
		} else {
			tmo.tv_sec = 0;
			tmo.tv_nsec = 200000000;
			assert(futex_twait(shlock, 0, CLOCK_REALTIME, &tmo,
			    (i & 2) ? FUTEX_PRIVATE_FLAG : 0) == -1);
			assert(errno == ETIMEDOUT);
			assert(waitpid(pid, &status, 0) == pid);
			assert(WIFEXITED(status));
			assert(WEXITSTATUS(status) == 0);
		}
	}

	assert(munmap(shlock, sizeof(*shlock)) == 0);

	/* Create anonymous memory for sharing a lock. */
	shlock = mmap(NULL, sizeof(*shlock), PROT_READ | PROT_WRITE,
	    MAP_ANON | MAP_SHARED, -1, 0);
	assert(shlock != MAP_FAILED);

	/* Wake another process. */
	pid = fork();
	assert(pid != -1);
	if (pid == 0) {
		usleep(50000);
		futex_wake(shlock, -1, 0);
		_exit(0);
	} else {
		assert(futex_twait(shlock, 0, 0, NULL, 0) == 0);
		assert(waitpid(pid, &status, 0) == pid);
		assert(WIFEXITED(status));
		assert(WEXITSTATUS(status) == 0);
	}

	/* Cannot wake another process using a private futex. */
	for (i = 1; i < 4; i++) {
		pid = fork();
		assert(pid != -1);
		if (pid == 0) {
			usleep(50000);
			futex_wake(shlock, -1,
			    (i & 1) ? FUTEX_PRIVATE_FLAG : 0);
			_exit(0);
		} else {
			tmo.tv_sec = 0;
			tmo.tv_nsec = 200000000;
			assert(futex_twait(shlock, 0, CLOCK_REALTIME, &tmo,
			    (i & 2) ? FUTEX_PRIVATE_FLAG : 0) == -1);
			assert(errno == ETIMEDOUT);
			assert(waitpid(pid, &status, 0) == pid);
			assert(WIFEXITED(status));
			assert(WEXITSTATUS(status) == 0);
		}
	}

	assert(munmap(shlock, sizeof(*shlock)) == 0);

	return 0;
}

void
handler(int sig)
{
}

void *
signaled(void *arg)
{
	/* Wait until receiving a signal. */
	assert(futex_twait(&lock, 0, 0, NULL, 0) == -1);
	assert(errno == EINTR);
	return NULL;
}

void *
awakener(void *arg)
{
	usleep(100);
	assert(futex_wake(&lock, -1, 0) == 1);
	return NULL;
}
