/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef UTIL_H
#define UTIL_H

#include <sys/socket.h>
#include <linux/vm_sockets.h>

/* Tests can either run as the client or the server */
enum test_mode {
	TEST_MODE_UNSET,
	TEST_MODE_CLIENT,
	TEST_MODE_SERVER
};

#define DEFAULT_PEER_PORT	1234

/* Test runner options */
struct test_opts {
	enum test_mode mode;
	unsigned int peer_cid;
	unsigned int peer_port;
};

/* A test case definition.  Test functions must print failures to stderr and
 * terminate with exit(EXIT_FAILURE).
 */
struct test_case {
	const char *name; /* human-readable name */

	/* Called when test mode is TEST_MODE_CLIENT */
	void (*run_client)(const struct test_opts *opts);

	/* Called when test mode is TEST_MODE_SERVER */
	void (*run_server)(const struct test_opts *opts);

	bool skip;
};

void init_signals(void);
unsigned int parse_cid(const char *str);
unsigned int parse_port(const char *str);
int vsock_stream_connect(unsigned int cid, unsigned int port);
int vsock_bind_connect(unsigned int cid, unsigned int port,
		       unsigned int bind_port, int type);
int vsock_seqpacket_connect(unsigned int cid, unsigned int port);
int vsock_stream_accept(unsigned int cid, unsigned int port,
			struct sockaddr_vm *clientaddrp);
int vsock_stream_listen(unsigned int cid, unsigned int port);
int vsock_seqpacket_accept(unsigned int cid, unsigned int port,
			   struct sockaddr_vm *clientaddrp);
void vsock_wait_remote_close(int fd);
void send_buf(int fd, const void *buf, size_t len, int flags,
	      ssize_t expected_ret);
void recv_buf(int fd, void *buf, size_t len, int flags, ssize_t expected_ret);
void send_byte(int fd, int expected_ret, int flags);
void recv_byte(int fd, int expected_ret, int flags);
void run_tests(const struct test_case *test_cases,
	       const struct test_opts *opts);
void list_tests(const struct test_case *test_cases);
void skip_test(struct test_case *test_cases, size_t test_cases_len,
	       const char *test_id_str);
unsigned long hash_djb2(const void *data, size_t len);
size_t iovec_bytes(const struct iovec *iov, size_t iovnum);
unsigned long iovec_hash_djb2(const struct iovec *iov, size_t iovnum);
struct iovec *alloc_test_iovec(const struct iovec *test_iovec, int iovnum);
void free_test_iovec(const struct iovec *test_iovec,
		     struct iovec *iovec, int iovnum);
#endif /* UTIL_H */
