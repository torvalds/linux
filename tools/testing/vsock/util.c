// SPDX-License-Identifier: GPL-2.0-only
/*
 * vsock test utilities
 *
 * Copyright (C) 2017 Red Hat, Inc.
 *
 * Author: Stefan Hajnoczi <stefanha@redhat.com>
 */

#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <assert.h>
#include <sys/epoll.h>
#include <sys/mman.h>

#include "timeout.h"
#include "control.h"
#include "util.h"

/* Install signal handlers */
void init_signals(void)
{
	struct sigaction act = {
		.sa_handler = sigalrm,
	};

	sigaction(SIGALRM, &act, NULL);
	signal(SIGPIPE, SIG_IGN);
}

static unsigned int parse_uint(const char *str, const char *err_str)
{
	char *endptr = NULL;
	unsigned long n;

	errno = 0;
	n = strtoul(str, &endptr, 10);
	if (errno || *endptr != '\0') {
		fprintf(stderr, "malformed %s \"%s\"\n", err_str, str);
		exit(EXIT_FAILURE);
	}
	return n;
}

/* Parse a CID in string representation */
unsigned int parse_cid(const char *str)
{
	return parse_uint(str, "CID");
}

/* Parse a port in string representation */
unsigned int parse_port(const char *str)
{
	return parse_uint(str, "port");
}

/* Wait for the remote to close the connection */
void vsock_wait_remote_close(int fd)
{
	struct epoll_event ev;
	int epollfd, nfds;

	epollfd = epoll_create1(0);
	if (epollfd == -1) {
		perror("epoll_create1");
		exit(EXIT_FAILURE);
	}

	ev.events = EPOLLRDHUP | EPOLLHUP;
	ev.data.fd = fd;
	if (epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev) == -1) {
		perror("epoll_ctl");
		exit(EXIT_FAILURE);
	}

	nfds = epoll_wait(epollfd, &ev, 1, TIMEOUT * 1000);
	if (nfds == -1) {
		perror("epoll_wait");
		exit(EXIT_FAILURE);
	}

	if (nfds == 0) {
		fprintf(stderr, "epoll_wait timed out\n");
		exit(EXIT_FAILURE);
	}

	assert(nfds == 1);
	assert(ev.events & (EPOLLRDHUP | EPOLLHUP));
	assert(ev.data.fd == fd);

	close(epollfd);
}

/* Create socket <type>, bind to <cid, port> and return the file descriptor. */
int vsock_bind(unsigned int cid, unsigned int port, int type)
{
	struct sockaddr_vm sa = {
		.svm_family = AF_VSOCK,
		.svm_cid = cid,
		.svm_port = port,
	};
	int fd;

	fd = socket(AF_VSOCK, type, 0);
	if (fd < 0) {
		perror("socket");
		exit(EXIT_FAILURE);
	}

	if (bind(fd, (struct sockaddr *)&sa, sizeof(sa))) {
		perror("bind");
		exit(EXIT_FAILURE);
	}

	return fd;
}

int vsock_connect_fd(int fd, unsigned int cid, unsigned int port)
{
	struct sockaddr_vm sa = {
		.svm_family = AF_VSOCK,
		.svm_cid = cid,
		.svm_port = port,
	};
	int ret;

	timeout_begin(TIMEOUT);
	do {
		ret = connect(fd, (struct sockaddr *)&sa, sizeof(sa));
		timeout_check("connect");
	} while (ret < 0 && errno == EINTR);
	timeout_end();

	return ret;
}

/* Bind to <bind_port>, connect to <cid, port> and return the file descriptor. */
int vsock_bind_connect(unsigned int cid, unsigned int port, unsigned int bind_port, int type)
{
	int client_fd;

	client_fd = vsock_bind(VMADDR_CID_ANY, bind_port, type);

	if (vsock_connect_fd(client_fd, cid, port)) {
		perror("connect");
		exit(EXIT_FAILURE);
	}

	return client_fd;
}

/* Connect to <cid, port> and return the file descriptor. */
int vsock_connect(unsigned int cid, unsigned int port, int type)
{
	int fd;

	control_expectln("LISTENING");

	fd = socket(AF_VSOCK, type, 0);
	if (fd < 0) {
		perror("socket");
		exit(EXIT_FAILURE);
	}

	if (vsock_connect_fd(fd, cid, port)) {
		int old_errno = errno;

		close(fd);
		fd = -1;
		errno = old_errno;
	}

	return fd;
}

int vsock_stream_connect(unsigned int cid, unsigned int port)
{
	return vsock_connect(cid, port, SOCK_STREAM);
}

int vsock_seqpacket_connect(unsigned int cid, unsigned int port)
{
	return vsock_connect(cid, port, SOCK_SEQPACKET);
}

/* Listen on <cid, port> and return the file descriptor. */
static int vsock_listen(unsigned int cid, unsigned int port, int type)
{
	int fd;

	fd = vsock_bind(cid, port, type);

	if (listen(fd, 1) < 0) {
		perror("listen");
		exit(EXIT_FAILURE);
	}

	return fd;
}

/* Listen on <cid, port> and return the first incoming connection.  The remote
 * address is stored to clientaddrp.  clientaddrp may be NULL.
 */
int vsock_accept(unsigned int cid, unsigned int port,
		 struct sockaddr_vm *clientaddrp, int type)
{
	union {
		struct sockaddr sa;
		struct sockaddr_vm svm;
	} clientaddr;
	socklen_t clientaddr_len = sizeof(clientaddr.svm);
	int fd, client_fd, old_errno;

	fd = vsock_listen(cid, port, type);

	control_writeln("LISTENING");

	timeout_begin(TIMEOUT);
	do {
		client_fd = accept(fd, &clientaddr.sa, &clientaddr_len);
		timeout_check("accept");
	} while (client_fd < 0 && errno == EINTR);
	timeout_end();

	old_errno = errno;
	close(fd);
	errno = old_errno;

	if (client_fd < 0)
		return client_fd;

	if (clientaddr_len != sizeof(clientaddr.svm)) {
		fprintf(stderr, "unexpected addrlen from accept(2), %zu\n",
			(size_t)clientaddr_len);
		exit(EXIT_FAILURE);
	}
	if (clientaddr.sa.sa_family != AF_VSOCK) {
		fprintf(stderr, "expected AF_VSOCK from accept(2), got %d\n",
			clientaddr.sa.sa_family);
		exit(EXIT_FAILURE);
	}

	if (clientaddrp)
		*clientaddrp = clientaddr.svm;
	return client_fd;
}

int vsock_stream_accept(unsigned int cid, unsigned int port,
			struct sockaddr_vm *clientaddrp)
{
	return vsock_accept(cid, port, clientaddrp, SOCK_STREAM);
}

int vsock_stream_listen(unsigned int cid, unsigned int port)
{
	return vsock_listen(cid, port, SOCK_STREAM);
}

int vsock_seqpacket_accept(unsigned int cid, unsigned int port,
			   struct sockaddr_vm *clientaddrp)
{
	return vsock_accept(cid, port, clientaddrp, SOCK_SEQPACKET);
}

/* Transmit bytes from a buffer and check the return value.
 *
 * expected_ret:
 *  <0 Negative errno (for testing errors)
 *   0 End-of-file
 *  >0 Success (bytes successfully written)
 */
void send_buf(int fd, const void *buf, size_t len, int flags,
	      ssize_t expected_ret)
{
	ssize_t nwritten = 0;
	ssize_t ret;

	timeout_begin(TIMEOUT);
	do {
		ret = send(fd, buf + nwritten, len - nwritten, flags);
		timeout_check("send");

		if (ret == 0 || (ret < 0 && errno != EINTR))
			break;

		nwritten += ret;
	} while (nwritten < len);
	timeout_end();

	if (expected_ret < 0) {
		if (ret != -1) {
			fprintf(stderr, "bogus send(2) return value %zd (expected %zd)\n",
				ret, expected_ret);
			exit(EXIT_FAILURE);
		}
		if (errno != -expected_ret) {
			perror("send");
			exit(EXIT_FAILURE);
		}
		return;
	}

	if (ret < 0) {
		perror("send");
		exit(EXIT_FAILURE);
	}

	if (nwritten != expected_ret) {
		if (ret == 0)
			fprintf(stderr, "unexpected EOF while sending bytes\n");

		fprintf(stderr, "bogus send(2) bytes written %zd (expected %zd)\n",
			nwritten, expected_ret);
		exit(EXIT_FAILURE);
	}
}

/* Receive bytes in a buffer and check the return value.
 *
 * expected_ret:
 *  <0 Negative errno (for testing errors)
 *   0 End-of-file
 *  >0 Success (bytes successfully read)
 */
void recv_buf(int fd, void *buf, size_t len, int flags, ssize_t expected_ret)
{
	ssize_t nread = 0;
	ssize_t ret;

	timeout_begin(TIMEOUT);
	do {
		ret = recv(fd, buf + nread, len - nread, flags);
		timeout_check("recv");

		if (ret == 0 || (ret < 0 && errno != EINTR))
			break;

		nread += ret;
	} while (nread < len);
	timeout_end();

	if (expected_ret < 0) {
		if (ret != -1) {
			fprintf(stderr, "bogus recv(2) return value %zd (expected %zd)\n",
				ret, expected_ret);
			exit(EXIT_FAILURE);
		}
		if (errno != -expected_ret) {
			perror("recv");
			exit(EXIT_FAILURE);
		}
		return;
	}

	if (ret < 0) {
		perror("recv");
		exit(EXIT_FAILURE);
	}

	if (nread != expected_ret) {
		if (ret == 0)
			fprintf(stderr, "unexpected EOF while receiving bytes\n");

		fprintf(stderr, "bogus recv(2) bytes read %zd (expected %zd)\n",
			nread, expected_ret);
		exit(EXIT_FAILURE);
	}
}

/* Transmit one byte and check the return value.
 *
 * expected_ret:
 *  <0 Negative errno (for testing errors)
 *   0 End-of-file
 *   1 Success
 */
void send_byte(int fd, int expected_ret, int flags)
{
	static const uint8_t byte = 'A';

	send_buf(fd, &byte, sizeof(byte), flags, expected_ret);
}

/* Receive one byte and check the return value.
 *
 * expected_ret:
 *  <0 Negative errno (for testing errors)
 *   0 End-of-file
 *   1 Success
 */
void recv_byte(int fd, int expected_ret, int flags)
{
	uint8_t byte;

	recv_buf(fd, &byte, sizeof(byte), flags, expected_ret);

	if (byte != 'A') {
		fprintf(stderr, "unexpected byte read 0x%02x\n", byte);
		exit(EXIT_FAILURE);
	}
}

/* Run test cases.  The program terminates if a failure occurs. */
void run_tests(const struct test_case *test_cases,
	       const struct test_opts *opts)
{
	int i;

	for (i = 0; test_cases[i].name; i++) {
		void (*run)(const struct test_opts *opts);
		char *line;

		printf("%d - %s...", i, test_cases[i].name);
		fflush(stdout);

		/* Full barrier before executing the next test.  This
		 * ensures that client and server are executing the
		 * same test case.  In particular, it means whoever is
		 * faster will not see the peer still executing the
		 * last test.  This is important because port numbers
		 * can be used by multiple test cases.
		 */
		if (test_cases[i].skip)
			control_writeln("SKIP");
		else
			control_writeln("NEXT");

		line = control_readln();
		if (control_cmpln(line, "SKIP", false) || test_cases[i].skip) {

			printf("skipped\n");

			free(line);
			continue;
		}

		control_cmpln(line, "NEXT", true);
		free(line);

		if (opts->mode == TEST_MODE_CLIENT)
			run = test_cases[i].run_client;
		else
			run = test_cases[i].run_server;

		if (run)
			run(opts);

		printf("ok\n");
	}
}

void list_tests(const struct test_case *test_cases)
{
	int i;

	printf("ID\tTest name\n");

	for (i = 0; test_cases[i].name; i++)
		printf("%d\t%s\n", i, test_cases[i].name);

	exit(EXIT_FAILURE);
}

static unsigned long parse_test_id(const char *test_id_str, size_t test_cases_len)
{
	unsigned long test_id;
	char *endptr = NULL;

	errno = 0;
	test_id = strtoul(test_id_str, &endptr, 10);
	if (errno || *endptr != '\0') {
		fprintf(stderr, "malformed test ID \"%s\"\n", test_id_str);
		exit(EXIT_FAILURE);
	}

	if (test_id >= test_cases_len) {
		fprintf(stderr, "test ID (%lu) larger than the max allowed (%lu)\n",
			test_id, test_cases_len - 1);
		exit(EXIT_FAILURE);
	}

	return test_id;
}

void skip_test(struct test_case *test_cases, size_t test_cases_len,
	       const char *test_id_str)
{
	unsigned long test_id = parse_test_id(test_id_str, test_cases_len);
	test_cases[test_id].skip = true;
}

void pick_test(struct test_case *test_cases, size_t test_cases_len,
	       const char *test_id_str)
{
	static bool skip_all = true;
	unsigned long test_id;

	if (skip_all) {
		unsigned long i;

		for (i = 0; i < test_cases_len; ++i)
			test_cases[i].skip = true;

		skip_all = false;
	}

	test_id = parse_test_id(test_id_str, test_cases_len);
	test_cases[test_id].skip = false;
}

unsigned long hash_djb2(const void *data, size_t len)
{
	unsigned long hash = 5381;
	int i = 0;

	while (i < len) {
		hash = ((hash << 5) + hash) + ((unsigned char *)data)[i];
		i++;
	}

	return hash;
}

size_t iovec_bytes(const struct iovec *iov, size_t iovnum)
{
	size_t bytes;
	int i;

	for (bytes = 0, i = 0; i < iovnum; i++)
		bytes += iov[i].iov_len;

	return bytes;
}

unsigned long iovec_hash_djb2(const struct iovec *iov, size_t iovnum)
{
	unsigned long hash;
	size_t iov_bytes;
	size_t offs;
	void *tmp;
	int i;

	iov_bytes = iovec_bytes(iov, iovnum);

	tmp = malloc(iov_bytes);
	if (!tmp) {
		perror("malloc");
		exit(EXIT_FAILURE);
	}

	for (offs = 0, i = 0; i < iovnum; i++) {
		memcpy(tmp + offs, iov[i].iov_base, iov[i].iov_len);
		offs += iov[i].iov_len;
	}

	hash = hash_djb2(tmp, iov_bytes);
	free(tmp);

	return hash;
}

/* Allocates and returns new 'struct iovec *' according pattern
 * in the 'test_iovec'. For each element in the 'test_iovec' it
 * allocates new element in the resulting 'iovec'. 'iov_len'
 * of the new element is copied from 'test_iovec'. 'iov_base' is
 * allocated depending on the 'iov_base' of 'test_iovec':
 *
 * 'iov_base' == NULL -> valid buf: mmap('iov_len').
 *
 * 'iov_base' == MAP_FAILED -> invalid buf:
 *               mmap('iov_len'), then munmap('iov_len').
 *               'iov_base' still contains result of
 *               mmap().
 *
 * 'iov_base' == number -> unaligned valid buf:
 *               mmap('iov_len') + number.
 *
 * 'iovnum' is number of elements in 'test_iovec'.
 *
 * Returns new 'iovec' or calls 'exit()' on error.
 */
struct iovec *alloc_test_iovec(const struct iovec *test_iovec, int iovnum)
{
	struct iovec *iovec;
	int i;

	iovec = malloc(sizeof(*iovec) * iovnum);
	if (!iovec) {
		perror("malloc");
		exit(EXIT_FAILURE);
	}

	for (i = 0; i < iovnum; i++) {
		iovec[i].iov_len = test_iovec[i].iov_len;

		iovec[i].iov_base = mmap(NULL, iovec[i].iov_len,
					 PROT_READ | PROT_WRITE,
					 MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE,
					 -1, 0);
		if (iovec[i].iov_base == MAP_FAILED) {
			perror("mmap");
			exit(EXIT_FAILURE);
		}

		if (test_iovec[i].iov_base != MAP_FAILED)
			iovec[i].iov_base += (uintptr_t)test_iovec[i].iov_base;
	}

	/* Unmap "invalid" elements. */
	for (i = 0; i < iovnum; i++) {
		if (test_iovec[i].iov_base == MAP_FAILED) {
			if (munmap(iovec[i].iov_base, iovec[i].iov_len)) {
				perror("munmap");
				exit(EXIT_FAILURE);
			}
		}
	}

	for (i = 0; i < iovnum; i++) {
		int j;

		if (test_iovec[i].iov_base == MAP_FAILED)
			continue;

		for (j = 0; j < iovec[i].iov_len; j++)
			((uint8_t *)iovec[i].iov_base)[j] = rand() & 0xff;
	}

	return iovec;
}

/* Frees 'iovec *', previously allocated by 'alloc_test_iovec()'.
 * On error calls 'exit()'.
 */
void free_test_iovec(const struct iovec *test_iovec,
		     struct iovec *iovec, int iovnum)
{
	int i;

	for (i = 0; i < iovnum; i++) {
		if (test_iovec[i].iov_base != MAP_FAILED) {
			if (test_iovec[i].iov_base)
				iovec[i].iov_base -= (uintptr_t)test_iovec[i].iov_base;

			if (munmap(iovec[i].iov_base, iovec[i].iov_len)) {
				perror("munmap");
				exit(EXIT_FAILURE);
			}
		}
	}

	free(iovec);
}

/* Set "unsigned long long" socket option and check that it's indeed set */
void setsockopt_ull_check(int fd, int level, int optname,
			  unsigned long long val, char const *errmsg)
{
	unsigned long long chkval;
	socklen_t chklen;
	int err;

	err = setsockopt(fd, level, optname, &val, sizeof(val));
	if (err) {
		fprintf(stderr, "setsockopt err: %s (%d)\n",
			strerror(errno), errno);
		goto fail;
	}

	chkval = ~val; /* just make storage != val */
	chklen = sizeof(chkval);

	err = getsockopt(fd, level, optname, &chkval, &chklen);
	if (err) {
		fprintf(stderr, "getsockopt err: %s (%d)\n",
			strerror(errno), errno);
		goto fail;
	}

	if (chklen != sizeof(chkval)) {
		fprintf(stderr, "size mismatch: set %zu got %d\n", sizeof(val),
			chklen);
		goto fail;
	}

	if (chkval != val) {
		fprintf(stderr, "value mismatch: set %llu got %llu\n", val,
			chkval);
		goto fail;
	}
	return;
fail:
	fprintf(stderr, "%s  val %llu\n", errmsg, val);
	exit(EXIT_FAILURE);
;
}

/* Set "int" socket option and check that it's indeed set */
void setsockopt_int_check(int fd, int level, int optname, int val,
			  char const *errmsg)
{
	int chkval;
	socklen_t chklen;
	int err;

	err = setsockopt(fd, level, optname, &val, sizeof(val));
	if (err) {
		fprintf(stderr, "setsockopt err: %s (%d)\n",
			strerror(errno), errno);
		goto fail;
	}

	chkval = ~val; /* just make storage != val */
	chklen = sizeof(chkval);

	err = getsockopt(fd, level, optname, &chkval, &chklen);
	if (err) {
		fprintf(stderr, "getsockopt err: %s (%d)\n",
			strerror(errno), errno);
		goto fail;
	}

	if (chklen != sizeof(chkval)) {
		fprintf(stderr, "size mismatch: set %zu got %d\n", sizeof(val),
			chklen);
		goto fail;
	}

	if (chkval != val) {
		fprintf(stderr, "value mismatch: set %d got %d\n", val, chkval);
		goto fail;
	}
	return;
fail:
	fprintf(stderr, "%s val %d\n", errmsg, val);
	exit(EXIT_FAILURE);
}

static void mem_invert(unsigned char *mem, size_t size)
{
	size_t i;

	for (i = 0; i < size; i++)
		mem[i] = ~mem[i];
}

/* Set "timeval" socket option and check that it's indeed set */
void setsockopt_timeval_check(int fd, int level, int optname,
			      struct timeval val, char const *errmsg)
{
	struct timeval chkval;
	socklen_t chklen;
	int err;

	err = setsockopt(fd, level, optname, &val, sizeof(val));
	if (err) {
		fprintf(stderr, "setsockopt err: %s (%d)\n",
			strerror(errno), errno);
		goto fail;
	}

	 /* just make storage != val */
	chkval = val;
	mem_invert((unsigned char *)&chkval, sizeof(chkval));
	chklen = sizeof(chkval);

	err = getsockopt(fd, level, optname, &chkval, &chklen);
	if (err) {
		fprintf(stderr, "getsockopt err: %s (%d)\n",
			strerror(errno), errno);
		goto fail;
	}

	if (chklen != sizeof(chkval)) {
		fprintf(stderr, "size mismatch: set %zu got %d\n", sizeof(val),
			chklen);
		goto fail;
	}

	if (memcmp(&chkval, &val, sizeof(val)) != 0) {
		fprintf(stderr, "value mismatch: set %ld:%ld got %ld:%ld\n",
			val.tv_sec, val.tv_usec, chkval.tv_sec, chkval.tv_usec);
		goto fail;
	}
	return;
fail:
	fprintf(stderr, "%s val %ld:%ld\n", errmsg, val.tv_sec, val.tv_usec);
	exit(EXIT_FAILURE);
}

void enable_so_zerocopy_check(int fd)
{
	setsockopt_int_check(fd, SOL_SOCKET, SO_ZEROCOPY, 1,
			     "setsockopt SO_ZEROCOPY");
}
