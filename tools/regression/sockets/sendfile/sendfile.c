/*-
 * Copyright (c) 2006 Robert N. M. Watson
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
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <netinet/in.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <md5.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * Simple regression test for sendfile.  Creates a file sized at four pages
 * and then proceeds to send it over a series of sockets, exercising a number
 * of cases and performing limited validation.
 */

#define FAIL(msg)	{printf("# %s\n", msg); \
			return (-1);}

#define FAIL_ERR(msg)	{printf("# %s: %s\n", msg, strerror(errno)); \
			return (-1);}

#define	TEST_PORT	5678
#define	TEST_MAGIC	0x4440f7bb
#define	TEST_PAGES	4
#define	TEST_SECONDS	30

struct test_header {
	uint32_t	th_magic;
	uint32_t	th_header_length;
	uint32_t	th_offset;
	uint32_t	th_length;
	char		th_md5[33];
};

struct sendfile_test {
	uint32_t	hdr_length;
	uint32_t	offset;
	uint32_t	length;
	uint32_t	file_size;
};

static int	file_fd;
static char	path[PATH_MAX];
static int	listen_socket;
static int	accept_socket;

static int test_th(struct test_header *th, uint32_t *header_length,
		uint32_t *offset, uint32_t *length);
static void signal_alarm(int signum);
static void setup_alarm(int seconds);
static void cancel_alarm(void);
static int receive_test(void);
static void run_child(void);
static int new_test_socket(int *connect_socket);
static void init_th(struct test_header *th, uint32_t header_length, 
		uint32_t offset, uint32_t length);
static int send_test(int connect_socket, struct sendfile_test);
static int write_test_file(size_t file_size);
static void run_parent(void);
static void cleanup(void);


static int
test_th(struct test_header *th, uint32_t *header_length, uint32_t *offset, 
		uint32_t *length)
{

	if (th->th_magic != htonl(TEST_MAGIC))
		FAIL("magic number not found in header")
	*header_length = ntohl(th->th_header_length);
	*offset = ntohl(th->th_offset);
	*length = ntohl(th->th_length);
	return (0);
}

static void
signal_alarm(int signum)
{
	(void)signum;

	printf("# test timeout\n");

	if (accept_socket > 0)
		close(accept_socket);
	if (listen_socket > 0)
		close(listen_socket);

	_exit(-1);
}

static void
setup_alarm(int seconds)
{
	struct itimerval itv;
	bzero(&itv, sizeof(itv));
	(void)seconds;
	itv.it_value.tv_sec = seconds;

	signal(SIGALRM, signal_alarm);
	setitimer(ITIMER_REAL, &itv, NULL);
}

static void
cancel_alarm(void)
{
	struct itimerval itv;
	bzero(&itv, sizeof(itv));
	setitimer(ITIMER_REAL, &itv, NULL);
}

static int
receive_test(void)
{
	uint32_t header_length, offset, length, counter;
	struct test_header th;
	ssize_t len;
	char buf[10240];
	MD5_CTX md5ctx;
	char *rxmd5;

	len = read(accept_socket, &th, sizeof(th));
	if (len < 0 || (size_t)len < sizeof(th))
		FAIL_ERR("read")

	if (test_th(&th, &header_length, &offset, &length) != 0)
		return (-1);

	MD5Init(&md5ctx);

	counter = 0;
	while (1) {
		len = read(accept_socket, buf, sizeof(buf));
		if (len < 0 || len == 0)
			break;
		counter += len;
		MD5Update(&md5ctx, buf, len);
	}

	rxmd5 = MD5End(&md5ctx, NULL);
	
	if ((counter != header_length+length) || 
			memcmp(th.th_md5, rxmd5, 33) != 0)
		FAIL("receive length mismatch")

	free(rxmd5);
	return (0);
}

static void
run_child(void)
{
	struct sockaddr_in sin;
	int rc = 0;

	listen_socket = socket(PF_INET, SOCK_STREAM, 0);
	if (listen_socket < 0) {
		printf("# socket: %s\n", strerror(errno));
		rc = -1;
	}

	if (!rc) {
		bzero(&sin, sizeof(sin));
		sin.sin_len = sizeof(sin);
		sin.sin_family = AF_INET;
		sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		sin.sin_port = htons(TEST_PORT);

		if (bind(listen_socket, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
			printf("# bind: %s\n", strerror(errno));
			rc = -1;
		}
	}

	if (!rc && listen(listen_socket, -1) < 0) {
		printf("# listen: %s\n", strerror(errno));
		rc = -1;
	}

	if (!rc) {
		accept_socket = accept(listen_socket, NULL, NULL);	
		setup_alarm(TEST_SECONDS);
		if (receive_test() != 0)
			rc = -1;
	}

	cancel_alarm();
	if (accept_socket > 0)
		close(accept_socket);
	if (listen_socket > 0)
		close(listen_socket);

	_exit(rc);
}

static int
new_test_socket(int *connect_socket)
{
	struct sockaddr_in sin;
	int rc = 0;

	*connect_socket = socket(PF_INET, SOCK_STREAM, 0);
	if (*connect_socket < 0)
		FAIL_ERR("socket")

	bzero(&sin, sizeof(sin));
	sin.sin_len = sizeof(sin);
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	sin.sin_port = htons(TEST_PORT);

	if (connect(*connect_socket, (struct sockaddr *)&sin, sizeof(sin)) < 0)
		FAIL_ERR("connect")

	return (rc);
}

static void
init_th(struct test_header *th, uint32_t header_length, uint32_t offset, 
		uint32_t length)
{
	bzero(th, sizeof(*th));
	th->th_magic = htonl(TEST_MAGIC);
	th->th_header_length = htonl(header_length);
	th->th_offset = htonl(offset);
	th->th_length = htonl(length);

	MD5FileChunk(path, th->th_md5, offset, length);
}

static int
send_test(int connect_socket, struct sendfile_test test)
{
	struct test_header th;
	struct sf_hdtr hdtr, *hdtrp;
	struct iovec headers;
	char *header;
	ssize_t len;
	int length;
	off_t off;

	len = lseek(file_fd, 0, SEEK_SET);
	if (len != 0)
		FAIL_ERR("lseek")

	struct stat st;
	if (fstat(file_fd, &st) < 0)
		FAIL_ERR("fstat")
	length = st.st_size - test.offset;
	if (test.length > 0 && test.length < (uint32_t)length)
		length = test.length;

	init_th(&th, test.hdr_length, test.offset, length);

	len = write(connect_socket, &th, sizeof(th));
	if (len != sizeof(th))
		return (-1);

	if (test.hdr_length != 0) {
		header = malloc(test.hdr_length);
		if (header == NULL)
			FAIL_ERR("malloc")

		hdtrp = &hdtr;
		bzero(&headers, sizeof(headers));
		headers.iov_base = header;
		headers.iov_len = test.hdr_length;
		bzero(&hdtr, sizeof(hdtr));
		hdtr.headers = &headers;
		hdtr.hdr_cnt = 1;
		hdtr.trailers = NULL;
		hdtr.trl_cnt = 0;
	} else {
		hdtrp = NULL;
		header = NULL;
	}

	if (sendfile(file_fd, connect_socket, test.offset, test.length, 
				hdtrp, &off, 0) < 0) {
		if (header != NULL)
			free(header);
		FAIL_ERR("sendfile")
	}

	if (length == 0) {
		struct stat sb;

		if (fstat(file_fd, &sb) == 0)
			length = sb.st_size - test.offset;
	}

	if (header != NULL)
		free(header);

	if (off != length)
		FAIL("offset != length")

	return (0);
}

static int
write_test_file(size_t file_size)
{
	char *page_buffer;
	ssize_t len;
	static size_t current_file_size = 0;

	if (file_size == current_file_size)
		return (0);
	else if (file_size < current_file_size) {
		if (ftruncate(file_fd, file_size) != 0)
			FAIL_ERR("ftruncate");
		current_file_size = file_size;
		return (0);
	}

	page_buffer = malloc(file_size);
	if (page_buffer == NULL)
		FAIL_ERR("malloc")
	bzero(page_buffer, file_size);

	len = write(file_fd, page_buffer, file_size);
	if (len < 0)
		FAIL_ERR("write")

	len = lseek(file_fd, 0, SEEK_SET);
	if (len < 0)
		FAIL_ERR("lseek")
	if (len != 0)
		FAIL("len != 0")

	free(page_buffer);
	current_file_size = file_size;
	return (0);
}

static void
run_parent(void)
{
	int connect_socket;
	int status;
	int test_num;
	int test_count;
	int pid;
	size_t desired_file_size = 0;

	const int pagesize = getpagesize();

	struct sendfile_test tests[] = {
 		{ .hdr_length = 0, .offset = 0, .length = 1 },
		{ .hdr_length = 0, .offset = 0, .length = pagesize },
		{ .hdr_length = 0, .offset = 1, .length = 1 },
		{ .hdr_length = 0, .offset = 1, .length = pagesize },
		{ .hdr_length = 0, .offset = pagesize, .length = pagesize },
		{ .hdr_length = 0, .offset = 0, .length = 2*pagesize },
		{ .hdr_length = 0, .offset = 0, .length = 0 },
		{ .hdr_length = 0, .offset = pagesize, .length = 0 },
		{ .hdr_length = 0, .offset = 2*pagesize, .length = 0 },
		{ .hdr_length = 0, .offset = TEST_PAGES*pagesize, .length = 0 },
		{ .hdr_length = 0, .offset = 0, .length = pagesize,
		    .file_size = 1 }
	};

	test_count = sizeof(tests) / sizeof(tests[0]);
	printf("1..%d\n", test_count);

	for (test_num = 1; test_num <= test_count; test_num++) {

		desired_file_size = tests[test_num - 1].file_size;
		if (desired_file_size == 0)
			desired_file_size = TEST_PAGES * pagesize;
		if (write_test_file(desired_file_size) != 0) {
			printf("not ok %d\n", test_num);
			continue;
		}

		pid = fork();
		if (pid == -1) {
			printf("not ok %d\n", test_num);
			continue;
		}

		if (pid == 0)
			run_child();

		usleep(250000);

		if (new_test_socket(&connect_socket) != 0) {
			printf("not ok %d\n", test_num);
			kill(pid, SIGALRM);
			close(connect_socket);
			continue;
		}

		if (send_test(connect_socket, tests[test_num-1]) != 0) {
			printf("not ok %d\n", test_num);
			kill(pid, SIGALRM);
			close(connect_socket);
			continue;
		}

		close(connect_socket);
		if (waitpid(pid, &status, 0) == pid) {
			if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
				printf("%s %d\n", "ok", test_num);
			else
				printf("%s %d\n", "not ok", test_num);
		}
		else {
			printf("not ok %d\n", test_num);
		}
	}
}

static void
cleanup(void)
{

	unlink(path);
}

int
main(int argc, char *argv[])
{

	path[0] = '\0';

	if (argc == 1) {
		snprintf(path, sizeof(path), "sendfile.XXXXXXXXXXXX");
		file_fd = mkstemp(path);
		if (file_fd == -1)
			FAIL_ERR("mkstemp");
	} else if (argc == 2) {
		(void)strlcpy(path, argv[1], sizeof(path));
		file_fd = open(path, O_CREAT | O_TRUNC | O_RDWR, 0600);
		if (file_fd == -1)
			FAIL_ERR("open");
	} else {
		FAIL("usage: sendfile [path]");
	}

	atexit(cleanup);

	run_parent();
	return (0);
}
