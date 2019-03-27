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
#include <assert.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * $FreeBSD$
 * This program tests to make sure that wraparound writes and reads
 * are working, assuming that 16K socket buffers are used.  In order
 * to really stress the pipe code with this test, kernel modifications
 * nay be necessary.
 */

int main (void)
{
	char buffer[32768], buffer2[32768], go[] = "go", go2[] = "go2";
	int desc[2], ipc_coord[2];
	ssize_t error, total;
	int buggy, i;
	pid_t new_pid;

	buggy = 0;
	total = 0;

	error = pipe(desc);
	if (error == -1)
		err(1, "Couldn't allocate data pipe");

	error = pipe(ipc_coord);
	if (error == -1)
		err(1, "Couldn't allocate IPC coordination pipe");

	buffer[0] = 'A';

	for (i = 1; i < (int)sizeof(buffer); i++) {
		buffer[i] = buffer[i - 1] + 1;
		if (buffer[i] > 'Z')
			buffer[i] = 'A';
	}

	new_pid = fork();
	assert(new_pid != -1);

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
	if ((_size = write(desc[1], &buffer[total], s)) != s)		\
		warn("short write; wrote %zd, expected %d", _size, s);	\
	total += _size;							\
	} while(0)

	if (new_pid == 0) {
		WRITE(4096);
		WRITE(4096);
		WRITE(4000);
		SYNC_W(0, go2);

		SYNC_R(0, go);
		WRITE(3000);
		WRITE(3000);
		SYNC_W(0, go2);

		_exit(0);
	}

	SYNC_R(1, go2);
	error = read(desc[0], &buffer2, 8192);
	total += error;
	printf("Read %zd bytes\n", error);
	SYNC_W(1, go);
	SYNC_R(1, go2);
	error = read(desc[0], &buffer2[total], 16384);
	total += error;
	printf("Read %zd bytes, done\n", error);

	if (memcmp(buffer, buffer2, total) != 0) {
		for (i = 0; i < total; i++) {
			if (buffer[i] != buffer2[i]) {
				buggy = 1;
				printf("Location %d input: %hhx output: %hhx\n",
				    i, buffer[i], buffer2[i]);
			}
		}
	}

	waitpid(new_pid, NULL, 0);

	if (buggy)
		errx(1, "FAILURE");

	printf("SUCCESS\n");

	exit(0);
}
