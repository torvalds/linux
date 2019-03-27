/*
Copyright (C) 2004 Michael J. Silbersack. All rights reserved.
 
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
SUCH DAMAGE.
*/

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/event.h>
#include <assert.h>
#include <err.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/*
 * $FreeBSD$
 * The goal of this program is to see if fstat reports the correct
 * data count for a pipe.  Prior to revision 1.172 of sys_pipe.c,
 * 0 would be returned once the pipe entered direct write mode.
 *
 * Linux (2.6) always returns zero, so it's not a valuable platform
 * for comparison.
 */

int
main(void)
{
	char buffer[32768], buffer2[32768], go[] = "go", go2[] = "go2";
	int desc[2], ipc_coord[2];
	struct kevent event, ke;
	ssize_t error;
	int successes = 0;
	struct stat status;
	pid_t new_pid;
	int kq;

	error = pipe(desc);
	if (error == -1)
		err(1, "Couldn't allocate data pipe");

	error = pipe(ipc_coord);
	if (error == -1)
		err(1, "Couldn't allocate IPC coordination pipe");

	new_pid = fork();
	assert(new_pid != -1);

	close(new_pid == 0 ? desc[0] : desc[1]);

#define	SYNC_R(i, _buf) do {	\
	int _error = errno; \
	warnx("%d: waiting for synchronization", __LINE__); \
	if (read(ipc_coord[i], &_buf, sizeof(_buf)) != sizeof(_buf)) \
		err(1, "failed to synchronize (%s)", (i == 0 ? "parent" : "child")); \
	errno = _error; \
	} while(0)

#define	SYNC_W(i, _buf) do {	\
	int _error = errno; \
	warnx("%d: sending synchronization", __LINE__); \
	if (write(ipc_coord[i], &_buf, sizeof(_buf)) != sizeof(_buf)) \
		err(1, "failed to synchronize (%s)", (i == 0 ? "child" : "parent")); \
	errno = _error; \
	} while(0)

#define	WRITE(s) do { 							\
	ssize_t _size; 							\
	if ((_size = write(desc[1], &buffer, s)) != s)			\
		warn("short write; wrote %zd, expected %d", _size, s);	\
	} while(0)

	if (new_pid == 0) {

		SYNC_R(0, go);
		WRITE(145);
		SYNC_W(0, go2);

		SYNC_R(0, go);
		WRITE(2048);
		SYNC_W(0, go2);

		SYNC_R(0, go);
		WRITE(4096);
		SYNC_W(0, go2);

		SYNC_R(0, go);
		WRITE(8191);
		SYNC_W(0, go2);

		SYNC_R(0, go);
		SYNC_W(0, go2); /* XXX: why is this required? */
		WRITE(8192);
		SYNC_W(0, go2);

		close(ipc_coord[0]);
		close(ipc_coord[1]);

		_exit(0);
	}

	kq = kqueue();
	if (kq == -1)
		_exit(1);

	EV_SET(&ke, desc[0], EVFILT_READ, EV_ADD, 0, 0, NULL);

	/* Attach event to the kqueue. */
	if (kevent(kq, &ke, 1, NULL, 0, NULL) != 0)
		_exit(2);

	while (successes < 5) {
		SYNC_W(1, go);
		SYNC_R(1, go2);

		/* Ensure data is available to read */
		if (kevent(kq, NULL, 0, &event, 1, NULL) != 1)
			_exit(3);

		fstat(desc[0], &status);
		error = read(desc[0], &buffer2, sizeof(buffer2));

		if (status.st_size != error)
			err(1, "FAILURE: stat size %jd read size %zd",
			    (intmax_t)status.st_size, error);
		if (error > 0) {
			printf("SUCCESS at stat size %jd read size %zd\n",
			    (intmax_t)status.st_size, error);
			successes++;
		}
	}

	exit(0);
}
