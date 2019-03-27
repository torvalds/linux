/*-
 * Copyright (c) 2018 Enji Cooper.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/uio.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <atf-c.h>

const char DETERMINISTIC_PATTERN[] =
    "The past is already gone, the future is not yet here. There's only one moment for you to live.\n";

#define	SOURCE_FILE		"source"
#define	DESTINATION_FILE	"dest"

#define	PORTRANGE_FIRST	"net.inet.ip.portrange.first"
#define	PORTRANGE_LAST	"net.inet.ip.portrange.last"

static int portrange_first, portrange_last;

static int
get_int_via_sysctlbyname(const char *oidname)
{
	size_t oldlen;
	int int_value;

	ATF_REQUIRE_EQ_MSG(sysctlbyname(oidname, &int_value, &oldlen, NULL, 0),
	    0, "sysctlbyname(%s, ...) failed: %s", oidname, strerror(errno));
	ATF_REQUIRE_EQ_MSG(sizeof(int_value), oldlen, "sanity check failed");

	return (int_value);
}

static int
generate_random_port(int seed)
{
	int random_port;

	printf("Generating a random port with seed=%d\n", seed);
	if (portrange_first == 0) {
		portrange_first = get_int_via_sysctlbyname(PORTRANGE_FIRST);
		printf("Port range lower bound: %d\n", portrange_first);
	}

	if (portrange_last == 0) {
		portrange_last = get_int_via_sysctlbyname(PORTRANGE_LAST);
		printf("Port range upper bound: %d\n", portrange_last);
	}

	srand((unsigned)seed);

	random_port = rand() % (portrange_last - portrange_first) +
	    portrange_first;

	printf("Random port generated: %d\n", random_port);
	return (random_port);
}

static void
resolve_localhost(struct addrinfo **res, int domain, int type, int port)
{
	const char *host;
	char *serv;
	struct addrinfo hints;
	int error;

	switch (domain) {
	case AF_INET:
		host = "127.0.0.1";
		break;
	case AF_INET6:
		host = "::1";
		break;
	default:
		atf_tc_fail("unhandled domain: %d", domain);
	}

	ATF_REQUIRE_MSG(asprintf(&serv, "%d", port) >= 0,
	    "asprintf failed: %s", strerror(errno));

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = domain;
	hints.ai_flags = AI_ADDRCONFIG|AI_NUMERICSERV|AI_NUMERICHOST;
	hints.ai_socktype = type;

	error = getaddrinfo(host, serv, &hints, res);
	ATF_REQUIRE_EQ_MSG(error, 0,
	    "getaddrinfo failed: %s", gai_strerror(error));
	free(serv);
}

static int
make_socket(int domain, int type, int protocol)
{
	int sock;

	sock = socket(domain, type, protocol);
	ATF_REQUIRE_MSG(sock != -1, "socket(%d, %d, 0) failed: %s",
	    domain, type, strerror(errno));

	return (sock);
}

static int
setup_client(int domain, int type, int port)
{
	struct addrinfo *res;
	char host[NI_MAXHOST+1];
	int error, sock;

	resolve_localhost(&res, domain, type, port);
	error = getnameinfo(
	    (const struct sockaddr*)res->ai_addr, res->ai_addrlen,
	    host, nitems(host) - 1, NULL, 0, NI_NUMERICHOST);
	ATF_REQUIRE_EQ_MSG(error, 0,
	    "getnameinfo failed: %s", gai_strerror(error));
	printf(
	    "Will try to connect to host='%s', address_family=%d, "
	    "socket_type=%d\n",
	    host, res->ai_family, res->ai_socktype);
	/* Avoid a double print when forked by flushing. */
	fflush(stdout);
	sock = make_socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	error = connect(sock, (struct sockaddr*)res->ai_addr, res->ai_addrlen);
	freeaddrinfo(res);
	ATF_REQUIRE_EQ_MSG(error, 0, "connect failed: %s", strerror(errno));
	return (sock);
}

/*
 * XXX: use linear probing to find a free port and eliminate `port` argument as
 * a [const] int (it will need to be a pointer so it can be passed back out of
 * the function and can influence which port `setup_client(..)` connects on.
 */
static int
setup_server(int domain, int type, int port)
{
	struct addrinfo *res;
	char host[NI_MAXHOST+1];
	int error, sock;

	resolve_localhost(&res, domain, type, port);
	sock = make_socket(res->ai_family, res->ai_socktype, res->ai_protocol);

	error = getnameinfo(
	    (const struct sockaddr*)res->ai_addr, res->ai_addrlen,
	    host, nitems(host) - 1, NULL, 0, NI_NUMERICHOST);
	ATF_REQUIRE_EQ_MSG(error, 0,
	    "getnameinfo failed: %s", gai_strerror(error));
	printf(
	    "Will try to bind socket to host='%s', address_family=%d, "
	    "socket_type=%d\n",
	    host, res->ai_family, res->ai_socktype);
	/* Avoid a double print when forked by flushing. */
	fflush(stdout);
	error = bind(sock, res->ai_addr, res->ai_addrlen);
	freeaddrinfo(res);
	ATF_REQUIRE_EQ_MSG(error, 0, "bind failed: %s", strerror(errno));
	error = listen(sock, 1);
	ATF_REQUIRE_EQ_MSG(error, 0, "listen failed: %s", strerror(errno));

	return (sock);
}

/*
 * This function is a helper routine for taking data being sent by `sendfile` via
 * `server_sock`, and pushing the received stream out to a file, denoted by
 * `dest_filename`.
 */
static void
server_cat(const char *dest_filename, int server_sock, size_t len)
{
	char *buffer, *buf_window_ptr;
	int recv_sock;
	size_t buffer_size;
	ssize_t received_bytes, recv_ret;

	/*
	 * Ensure that there isn't excess data sent across the wire by
	 * capturing 10 extra bytes (plus 1 for nul).
	 */
	buffer_size = len + 10 + 1;
	buffer = calloc(buffer_size, sizeof(char));
	if (buffer == NULL)
		err(1, "malloc failed");

	recv_sock = accept(server_sock, NULL, 0);
	if (recv_sock == -1)
		err(1, "accept failed");

	buf_window_ptr = buffer;
	received_bytes = 0;
	do {
		recv_ret = recv(recv_sock, buf_window_ptr,
		    buffer_size - received_bytes, 0);
		if (recv_ret <= 0)
			break;
		buf_window_ptr += recv_ret;
		received_bytes += recv_ret;
	} while (received_bytes < buffer_size);

	atf_utils_create_file(dest_filename, "%s", buffer);

	(void)close(recv_sock);
	(void)close(server_sock);
	free(buffer);

	if (received_bytes != len)
		errx(1, "received unexpected data: %zd != %zd", received_bytes,
		    len);
}

static int
setup_tcp_server(int domain, int port)
{

	return (setup_server(domain, SOCK_STREAM, port));
}

static int
setup_tcp_client(int domain, int port)
{

	return (setup_client(domain, SOCK_STREAM, port));
}

static off_t
file_size_from_fd(int fd)
{
	struct stat st;

	ATF_REQUIRE_EQ_MSG(0, fstat(fd, &st),
	    "fstat failed: %s", strerror(errno));

	return (st.st_size);
}

/*
 * NB: `nbytes` == 0 has special connotations given the sendfile(2) API
 * contract. In short, "send the whole file" (paraphrased).
 */
static void
verify_source_and_dest(const char* dest_filename, int src_fd, off_t offset,
    size_t nbytes)
{
	char *dest_pointer, *src_pointer;
	off_t dest_file_size, src_file_size;
	size_t length;
	int dest_fd;

	atf_utils_cat_file(dest_filename, "dest_file: ");

	dest_fd = open(dest_filename, O_RDONLY);
	ATF_REQUIRE_MSG(dest_fd != -1, "open failed");

	dest_file_size = file_size_from_fd(dest_fd);
	src_file_size = file_size_from_fd(src_fd);

	/*
	 * Per sendfile(2), "send the whole file" (paraphrased). This means
	 * that we need to grab the file size, as passing in length = 0 with
	 * mmap(2) will result in a failure with EINVAL (length = 0 is invalid).
	 */
	length = (nbytes == 0) ? (size_t)(src_file_size - offset) : nbytes;

	ATF_REQUIRE_EQ_MSG(dest_file_size, length,
	    "number of bytes written out to %s (%ju) doesn't match the "
	    "expected number of bytes (%zu)", dest_filename, dest_file_size,
	    length);

	ATF_REQUIRE_EQ_MSG(0, lseek(src_fd, offset, SEEK_SET),
	    "lseek failed: %s", strerror(errno));

	dest_pointer = mmap(NULL, length, PROT_READ, MAP_PRIVATE, dest_fd, 0);
	ATF_REQUIRE_MSG(dest_pointer != MAP_FAILED, "mmap failed: %s",
	    strerror(errno));

	printf("Will mmap in the source file from offset=%jd to length=%zu\n",
	    offset, length);

	src_pointer = mmap(NULL, length, PROT_READ, MAP_PRIVATE, src_fd, offset);
	ATF_REQUIRE_MSG(src_pointer != MAP_FAILED, "mmap failed: %s",
	    strerror(errno));

	ATF_REQUIRE_EQ_MSG(0, memcmp(src_pointer, dest_pointer, length),
	    "Contents of source and destination do not match. '%s' != '%s'",
	    src_pointer, dest_pointer);

	(void)munmap(src_pointer, length);
	(void)munmap(dest_pointer, length);
	(void)close(dest_fd);
}

static void
fd_positive_file_test(int domain)
{
	off_t offset;
	size_t nbytes, pattern_size;
	int client_sock, error, fd, port, server_sock;
	pid_t server_pid;

	pattern_size = strlen(DETERMINISTIC_PATTERN);

	atf_utils_create_file(SOURCE_FILE, "%s", DETERMINISTIC_PATTERN);
	fd = open(SOURCE_FILE, O_RDONLY);
	ATF_REQUIRE_MSG(fd != -1, "open failed: %s", strerror(errno));

	port = generate_random_port(__LINE__ + domain);
	server_sock = setup_tcp_server(domain, port);
	client_sock = setup_tcp_client(domain, port);

	server_pid = atf_utils_fork();
	if (server_pid == 0) {
		(void)close(client_sock);
		server_cat(DESTINATION_FILE, server_sock, pattern_size);
		_exit(0);
	} else
		(void)close(server_sock);

	nbytes = 0;
	offset = 0;
	error = sendfile(fd, client_sock, offset, nbytes, NULL, NULL,
	    SF_FLAGS(0, 0));
	ATF_REQUIRE_EQ_MSG(0, error, "sendfile failed: %s", strerror(errno));
	(void)close(client_sock);

	atf_utils_wait(server_pid, 0, "", "");
	verify_source_and_dest(DESTINATION_FILE, fd, offset, nbytes);

	(void)close(fd);
}

ATF_TC(fd_positive_file_v4);
ATF_TC_HEAD(fd_positive_file_v4, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "Verify regular file as file descriptor support (IPv4)");
}
ATF_TC_BODY(fd_positive_file_v4, tc)
{

	fd_positive_file_test(AF_INET);
}

ATF_TC(fd_positive_file_v6);
ATF_TC_HEAD(fd_positive_file_v6, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "Verify regular file as file descriptor support (IPv6)");
}
ATF_TC_BODY(fd_positive_file_v6, tc)
{

	fd_positive_file_test(AF_INET6);
}

static void
fd_positive_shm_test(int domain)
{
	char *shm_pointer;
	off_t offset;
	size_t nbytes, pattern_size;
	pid_t server_pid;
	int client_sock, error, fd, port, server_sock;

	pattern_size = strlen(DETERMINISTIC_PATTERN);

	printf("pattern size: %zu\n", pattern_size);

	fd = shm_open(SHM_ANON, O_RDWR|O_CREAT, 0600);
	ATF_REQUIRE_MSG(fd != -1, "shm_open failed: %s", strerror(errno));
	ATF_REQUIRE_EQ_MSG(0, ftruncate(fd, pattern_size),
	    "ftruncate failed: %s", strerror(errno));
	shm_pointer = mmap(NULL, pattern_size, PROT_READ|PROT_WRITE,
	    MAP_SHARED, fd, 0);
	ATF_REQUIRE_MSG(shm_pointer != MAP_FAILED,
	    "mmap failed: %s", strerror(errno));
	memcpy(shm_pointer, DETERMINISTIC_PATTERN, pattern_size);
	ATF_REQUIRE_EQ_MSG(0,
	    memcmp(shm_pointer, DETERMINISTIC_PATTERN, pattern_size),
	    "memcmp showed data mismatch: '%s' != '%s'",
	    DETERMINISTIC_PATTERN, shm_pointer);

	port = generate_random_port(__LINE__ + domain);
	server_sock = setup_tcp_server(domain, port);
	client_sock = setup_tcp_client(domain, port);

	server_pid = atf_utils_fork();
	if (server_pid == 0) {
		(void)close(client_sock);
		server_cat(DESTINATION_FILE, server_sock, pattern_size);
		_exit(0);
	} else
		(void)close(server_sock);

	nbytes = 0;
	offset = 0;
	error = sendfile(fd, client_sock, offset, nbytes, NULL, NULL,
	    SF_FLAGS(0, 0));
	ATF_REQUIRE_EQ_MSG(0, error, "sendfile failed: %s", strerror(errno));
	(void)close(client_sock);

	atf_utils_wait(server_pid, 0, "", "");
	verify_source_and_dest(DESTINATION_FILE, fd, offset, nbytes);

	(void)munmap(shm_pointer, sizeof(DETERMINISTIC_PATTERN));
	(void)close(fd);
}

ATF_TC(fd_positive_shm_v4);
ATF_TC_HEAD(fd_positive_shm_v4, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "Verify shared memory as file descriptor support (IPv4)");
}
ATF_TC_BODY(fd_positive_shm_v4, tc)
{

	fd_positive_shm_test(AF_INET);
}

ATF_TC(fd_positive_shm_v6);
ATF_TC_HEAD(fd_positive_shm_v6, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "Verify shared memory as file descriptor support (IPv6))");
}
ATF_TC_BODY(fd_positive_shm_v6, tc)
{

	fd_positive_shm_test(AF_INET6);
}

static void
fd_negative_bad_fd_test(int domain)
{
	int client_sock, error, fd, port, server_sock;

	port = generate_random_port(__LINE__ + domain);
	server_sock = setup_tcp_server(domain, port);
	client_sock = setup_tcp_client(domain, port);

	fd = -1;

	error = sendfile(fd, client_sock, 0, 0, NULL, NULL, SF_FLAGS(0, 0));
	ATF_REQUIRE_ERRNO(EBADF, error == -1);

	(void)close(client_sock);
	(void)close(server_sock);
}

ATF_TC(fd_negative_bad_fd_v4);
ATF_TC_HEAD(fd_negative_bad_fd_v4, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "Verify bad file descriptor returns EBADF (IPv4)");
}
ATF_TC_BODY(fd_negative_bad_fd_v4, tc)
{

	fd_negative_bad_fd_test(AF_INET);
}

ATF_TC(fd_negative_bad_fd_v6);
ATF_TC_HEAD(fd_negative_bad_fd_v6, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "Verify bad file descriptor returns EBADF (IPv6)");
}
ATF_TC_BODY(fd_negative_bad_fd_v6, tc)
{

	fd_negative_bad_fd_test(AF_INET6);
}

static void
flags_test(int domain)
{
	off_t offset;
	size_t nbytes, pattern_size;
	int client_sock, error, fd, i, port, server_sock;
	pid_t server_pid;
	int16_t number_pages = 10;

	pattern_size = strlen(DETERMINISTIC_PATTERN);

	struct testcase {
		int16_t readahead_pages, flags;
	} testcases[] = {
		/* This is covered in `:fd_positive_file` */
#if 0
		{
			.readahead_pages = 0,
			.flags = 0
		},
#endif
		{
			.readahead_pages = 0,
			.flags = SF_NOCACHE
		},
#ifdef SF_USER_READAHEAD
		{
			.readahead_pages = 0,
			.flags = SF_NOCACHE|SF_USER_READAHEAD
		},
		{
			.readahead_pages = 0,
			.flags = SF_USER_READAHEAD
		},
#endif
		{
			.readahead_pages = number_pages,
			.flags = 0
		},
		{
			.readahead_pages = number_pages,
			.flags = SF_NOCACHE
		},
#ifdef SF_USER_READAHEAD
		{
			.readahead_pages = number_pages,
			.flags = SF_NOCACHE|SF_USER_READAHEAD
		},
#endif
		{
			.readahead_pages = number_pages,
			.flags = SF_NOCACHE
		},
		{
			.readahead_pages = number_pages,
			.flags = SF_NODISKIO
		}
	};

	atf_utils_create_file(SOURCE_FILE, "%s", DETERMINISTIC_PATTERN);
	for (i = 0; i < nitems(testcases); i++) {
		fd = open(SOURCE_FILE, O_RDONLY);
		ATF_REQUIRE_MSG(fd != -1, "open failed: %s", strerror(errno));

		port = generate_random_port(i * __LINE__ + domain);
		server_sock = setup_tcp_server(domain, port);
		client_sock = setup_tcp_client(domain, port);

		server_pid = atf_utils_fork();
		if (server_pid == 0) {
			(void)close(client_sock);
			server_cat(DESTINATION_FILE, server_sock, pattern_size);
			_exit(0);
		} else
			(void)close(server_sock);

		nbytes = 0;
		offset = 0;
		error = sendfile(fd, client_sock, offset, nbytes, NULL, NULL,
		    SF_FLAGS(testcases[i].readahead_pages, testcases[i].flags));
		ATF_CHECK_EQ_MSG(error, 0, "sendfile testcase #%d failed: %s",
		    i, strerror(errno));
		(void)close(client_sock);

		atf_utils_wait(server_pid, 0, "", "");
		verify_source_and_dest(DESTINATION_FILE, fd, offset, nbytes);

		(void)close(fd);
	}
}

ATF_TC(flags_v4);
ATF_TC_HEAD(flags_v4, tc)
{

	atf_tc_set_md_var(tc, "descr", "Verify flags functionality (IPv4)");
}
ATF_TC_BODY(flags_v4, tc)
{

	flags_test(AF_INET);
}

ATF_TC(flags_v6);
ATF_TC_HEAD(flags_v6, tc)
{

	atf_tc_set_md_var(tc, "descr", "Verify flags functionality (IPv6)");
}
ATF_TC_BODY(flags_v6, tc)
{

	flags_test(AF_INET6);
}

static void
hdtr_positive_test(int domain)
{
	struct iovec headers[1], trailers[1];
	struct testcase {
		bool include_headers, include_trailers;
	} testcases[] = {
		/* This is covered in `:fd_positive_file` */
#if 0
		{
			.include_headers = false,
			.include_trailers = false
		},
#endif
		{
			.include_headers = true,
			.include_trailers = false
		},
		{
			.include_headers = false,
			.include_trailers = true
		},
		{
			.include_headers = true,
			.include_trailers = true
		}
	};
	off_t offset;
	size_t nbytes;
	int client_sock, error, fd, fd2, i, port, rc, server_sock;
	pid_t server_pid;

	headers[0].iov_base = "This is a header";
	headers[0].iov_len = strlen(headers[0].iov_base);
	trailers[0].iov_base = "This is a trailer";
	trailers[0].iov_len = strlen(trailers[0].iov_base);
	offset = 0;
	nbytes = 0;

	for (i = 0; i < nitems(testcases); i++) {
		struct sf_hdtr hdtr;
		char *pattern;

		if (testcases[i].include_headers) {
			hdtr.headers = headers;
			hdtr.hdr_cnt = nitems(headers);
		} else {
			hdtr.headers = NULL;
			hdtr.hdr_cnt = 0;
		}

		if (testcases[i].include_trailers) {
			hdtr.trailers = trailers;
			hdtr.trl_cnt = nitems(trailers);
		} else {
			hdtr.trailers = NULL;
			hdtr.trl_cnt = 0;
		}

		port = generate_random_port(i * __LINE__ + domain);
		server_sock = setup_tcp_server(domain, port);
		client_sock = setup_tcp_client(domain, port);

		rc = asprintf(&pattern, "%s%s%s",
		    testcases[i].include_headers ? (char *)headers[0].iov_base : "",
		    DETERMINISTIC_PATTERN,
		    testcases[i].include_trailers ? (char *)trailers[0].iov_base : "");
		ATF_REQUIRE_MSG(rc != -1, "asprintf failed: %s", strerror(errno));

		atf_utils_create_file(SOURCE_FILE ".full", "%s", pattern);
		atf_utils_create_file(SOURCE_FILE, "%s", DETERMINISTIC_PATTERN);

		fd = open(SOURCE_FILE, O_RDONLY);
		ATF_REQUIRE_MSG(fd != -1, "open failed: %s", strerror(errno));

		fd2 = open(SOURCE_FILE ".full", O_RDONLY);
		ATF_REQUIRE_MSG(fd2 != -1, "open failed: %s", strerror(errno));

		server_pid = atf_utils_fork();
		if (server_pid == 0) {
			(void)close(client_sock);
			server_cat(DESTINATION_FILE, server_sock,
			    strlen(pattern));
			_exit(0);
		} else
			(void)close(server_sock);

		error = sendfile(fd, client_sock, offset, nbytes, &hdtr,
		    NULL, SF_FLAGS(0, 0));
		ATF_CHECK_EQ_MSG(error, 0, "sendfile testcase #%d failed: %s",
		    i, strerror(errno));
		(void)close(client_sock);

		atf_utils_wait(server_pid, 0, "", "");
		verify_source_and_dest(DESTINATION_FILE, fd2, offset, nbytes);

		(void)close(fd);
		(void)close(fd2);
		free(pattern);
		pattern = NULL;
	}
}

ATF_TC(hdtr_positive_v4);
ATF_TC_HEAD(hdtr_positive_v4, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "Verify positive hdtr functionality (IPv4)");
}
ATF_TC_BODY(hdtr_positive_v4, tc)
{

	hdtr_positive_test(AF_INET);
}

ATF_TC(hdtr_positive_v6);
ATF_TC_HEAD(hdtr_positive_v6, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "Verify positive hdtr functionality (IPv6)");
}
ATF_TC_BODY(hdtr_positive_v6, tc)
{

	hdtr_positive_test(AF_INET);
}

static void
hdtr_negative_bad_pointers_test(int domain)
{
	int client_sock, error, fd, port, server_sock;
	struct sf_hdtr *hdtr1, hdtr2, hdtr3;

	port = generate_random_port(__LINE__ + domain);

	hdtr1 = (struct sf_hdtr*)-1;

	memset(&hdtr2, 0, sizeof(hdtr2));
	hdtr2.hdr_cnt = 1;
	hdtr2.headers = (struct iovec*)-1;

	memset(&hdtr3, 0, sizeof(hdtr3));
	hdtr3.trl_cnt = 1;
	hdtr3.trailers = (struct iovec*)-1;

	fd = open(SOURCE_FILE, O_CREAT|O_RDWR);
	ATF_REQUIRE_MSG(fd != -1, "open failed: %s", strerror(errno));

	server_sock = setup_tcp_server(domain, port);
	client_sock = setup_tcp_client(domain, port);

	error = sendfile(fd, client_sock, 0, 0, hdtr1, NULL, SF_FLAGS(0, 0));
	ATF_CHECK_ERRNO(EFAULT, error == -1);

	error = sendfile(fd, client_sock, 0, 0, &hdtr2, NULL, SF_FLAGS(0, 0));
	ATF_CHECK_ERRNO(EFAULT, error == -1);

	error = sendfile(fd, client_sock, 0, 0, &hdtr3, NULL, SF_FLAGS(0, 0));
	ATF_CHECK_ERRNO(EFAULT, error == -1);

	(void)close(fd);
	(void)close(client_sock);
	(void)close(server_sock);
}

ATF_TC(hdtr_negative_bad_pointers_v4);
ATF_TC_HEAD(hdtr_negative_bad_pointers_v4, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "Verify that bad pointers for hdtr storage result in EFAULT (IPv4)");
}
ATF_TC_BODY(hdtr_negative_bad_pointers_v4, tc)
{

	hdtr_negative_bad_pointers_test(AF_INET);
}

ATF_TC(hdtr_negative_bad_pointers_v6);
ATF_TC_HEAD(hdtr_negative_bad_pointers_v6, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "Verify that bad pointers for hdtr storage result in EFAULT (IPv6)");
}
ATF_TC_BODY(hdtr_negative_bad_pointers_v6, tc)
{

	hdtr_negative_bad_pointers_test(AF_INET6);
}

static void
offset_negative_value_less_than_zero_test(int domain)
{
	int client_sock, error, fd, port, server_sock;

	port = generate_random_port(__LINE__ + domain);
	server_sock = setup_tcp_server(domain, port);
	client_sock = setup_tcp_client(domain, port);

	fd = open(SOURCE_FILE, O_CREAT|O_RDWR);
	ATF_REQUIRE_MSG(fd != -1, "open failed: %s", strerror(errno));

	error = sendfile(fd, client_sock, -1, 0, NULL, NULL, SF_FLAGS(0, 0));
	ATF_REQUIRE_ERRNO(EINVAL, error == -1);

	(void)close(fd);
	(void)close(client_sock);
	(void)close(server_sock);
}

ATF_TC(offset_negative_value_less_than_zero_v4);
ATF_TC_HEAD(offset_negative_value_less_than_zero_v4, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "Verify that a negative offset results in EINVAL (IPv4)");
}
ATF_TC_BODY(offset_negative_value_less_than_zero_v4, tc)
{

	offset_negative_value_less_than_zero_test(AF_INET);
}

ATF_TC(offset_negative_value_less_than_zero_v6);
ATF_TC_HEAD(offset_negative_value_less_than_zero_v6, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "Verify that a negative offset results in EINVAL (IPv6)");
}
ATF_TC_BODY(offset_negative_value_less_than_zero_v6, tc)
{

	offset_negative_value_less_than_zero_test(AF_INET6);
}

static void
sbytes_positive_test(int domain)
{
	size_t pattern_size = strlen(DETERMINISTIC_PATTERN);
	off_t sbytes;
	int client_sock, error, fd, port, server_sock;

	port = generate_random_port(__LINE__ + domain);
	server_sock = setup_tcp_server(domain, port);
	client_sock = setup_tcp_client(domain, port);

	atf_utils_create_file(SOURCE_FILE, "%s", DETERMINISTIC_PATTERN);
	fd = open(SOURCE_FILE, O_RDONLY);
	ATF_REQUIRE_MSG(fd != -1, "open failed: %s", strerror(errno));

	error = sendfile(fd, client_sock, 0, 0, NULL, &sbytes, SF_FLAGS(0, 0));
	ATF_CHECK_EQ_MSG(error, 0, "sendfile failed: %s", strerror(errno));

	(void)close(fd);
	(void)close(client_sock);
	(void)close(server_sock);

	ATF_CHECK_EQ_MSG(pattern_size, sbytes,
	    "the value returned by sbytes does not match the expected pattern "
	    "size");
}

ATF_TC(sbytes_positive_v4);
ATF_TC_HEAD(sbytes_positive_v4, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "Verify positive `sbytes` functionality (IPv4)");
}
ATF_TC_BODY(sbytes_positive_v4, tc)
{

	sbytes_positive_test(AF_INET);
}

ATF_TC(sbytes_positive_v6);
ATF_TC_HEAD(sbytes_positive_v6, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "Verify positive `sbytes` functionality (IPv6)");
}
ATF_TC_BODY(sbytes_positive_v6, tc)
{

	sbytes_positive_test(AF_INET6);
}

static void
sbytes_negative_test(int domain)
{
	off_t *sbytes_p = (off_t*)-1;
	int client_sock, error, fd, port, server_sock;

	port = generate_random_port(__LINE__ + domain);
	server_sock = setup_tcp_server(domain, port);
	client_sock = setup_tcp_client(domain, port);

	atf_utils_create_file(SOURCE_FILE, "%s", DETERMINISTIC_PATTERN);
	fd = open(SOURCE_FILE, O_RDONLY);
	ATF_REQUIRE_MSG(fd != -1, "open failed: %s", strerror(errno));

	atf_tc_expect_fail(
	    "bug 232210: EFAULT assert fails because copyout(9) call is not checked");

	error = sendfile(fd, client_sock, 0, 0, NULL, sbytes_p, SF_FLAGS(0, 0));
	ATF_REQUIRE_ERRNO(EFAULT, error == -1);

	(void)close(fd);
	(void)close(client_sock);
	(void)close(server_sock);
}

ATF_TC(sbytes_negative_v4);
ATF_TC_HEAD(sbytes_negative_v4, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "Verify negative `sbytes` functionality (IPv4)");
}
ATF_TC_BODY(sbytes_negative_v4, tc)
{

	sbytes_negative_test(AF_INET);
}

ATF_TC(sbytes_negative_v6);
ATF_TC_HEAD(sbytes_negative_v6, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "Verify negative `sbytes` functionality (IPv6)");
}
ATF_TC_BODY(sbytes_negative_v6, tc)
{

	sbytes_negative_test(AF_INET6);
}

static void
s_negative_not_connected_socket_test(int domain)
{
	int client_sock, error, fd, port;

	port = generate_random_port(__LINE__ + domain);
	client_sock = setup_tcp_server(domain, port);

	fd = open(SOURCE_FILE, O_CREAT|O_RDWR);
	ATF_REQUIRE_MSG(fd != -1, "open failed: %s", strerror(errno));

	error = sendfile(fd, client_sock, 0, 0, NULL, NULL, SF_FLAGS(0, 0));
	ATF_REQUIRE_ERRNO(ENOTCONN, error == -1);

	(void)close(fd);
	(void)close(client_sock);
}

ATF_TC(s_negative_not_connected_socket_v4);
ATF_TC_HEAD(s_negative_not_connected_socket_v4, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "Verify that a non-connected SOCK_STREAM socket results in ENOTCONN (IPv4)");
}

ATF_TC_BODY(s_negative_not_connected_socket_v4, tc)
{

	s_negative_not_connected_socket_test(AF_INET);
}

ATF_TC(s_negative_not_connected_socket_v6);
ATF_TC_HEAD(s_negative_not_connected_socket_v6, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "Verify that a non-connected SOCK_STREAM socket results in ENOTCONN (IPv6)");
}

ATF_TC_BODY(s_negative_not_connected_socket_v6, tc)
{

	s_negative_not_connected_socket_test(AF_INET6);
}

ATF_TC(s_negative_not_descriptor);
ATF_TC_HEAD(s_negative_not_descriptor, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "Verify that an invalid file descriptor, e.g., -1, fails with EBADF");
}

ATF_TC_BODY(s_negative_not_descriptor, tc)
{
	int client_sock, error, fd;

	client_sock = -1;

	fd = open(SOURCE_FILE, O_CREAT|O_RDWR);
	ATF_REQUIRE_MSG(fd != -1, "open failed: %s", strerror(errno));

	error = sendfile(fd, client_sock, 0, 0, NULL, NULL, SF_FLAGS(0, 0));
	ATF_REQUIRE_ERRNO(EBADF, error == -1);

	(void)close(fd);
}

ATF_TC(s_negative_not_socket_file_descriptor);
ATF_TC_HEAD(s_negative_not_socket_file_descriptor, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "Verify that a non-socket file descriptor fails with ENOTSOCK");
}

ATF_TC_BODY(s_negative_not_socket_file_descriptor, tc)
{
	int client_sock, error, fd;

	fd = open(SOURCE_FILE, O_CREAT|O_RDWR);
	ATF_REQUIRE_MSG(fd != -1, "open failed: %s", strerror(errno));

	client_sock = open(_PATH_DEVNULL, O_WRONLY);
	ATF_REQUIRE_MSG(fd != -1, "open failed: %s", strerror(errno));

	error = sendfile(fd, client_sock, 0, 0, NULL, NULL, SF_FLAGS(0, 0));
	ATF_REQUIRE_ERRNO(ENOTSOCK, error == -1);

	(void)close(fd);
	(void)close(client_sock);
}

static void
s_negative_udp_socket_test(int domain)
{
	int client_sock, error, fd, port;

	port = generate_random_port(__LINE__ + domain);
	client_sock = setup_client(domain, SOCK_DGRAM, port);

	fd = open(SOURCE_FILE, O_CREAT|O_RDWR);
	ATF_REQUIRE_MSG(fd != -1, "open failed: %s", strerror(errno));

	error = sendfile(fd, client_sock, 0, 0, NULL, NULL, SF_FLAGS(0, 0));
	ATF_REQUIRE_ERRNO(EINVAL, error == -1);

	(void)close(fd);
	(void)close(client_sock);
}

ATF_TC(s_negative_udp_socket_v4);
ATF_TC_HEAD(s_negative_udp_socket_v4, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "Verify that a non-SOCK_STREAM type socket results in EINVAL (IPv4)");
}
ATF_TC_BODY(s_negative_udp_socket_v4, tc)
{

	s_negative_udp_socket_test(AF_INET);
}

ATF_TC(s_negative_udp_socket_v6);
ATF_TC_HEAD(s_negative_udp_socket_v6, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "Verify that a non-SOCK_STREAM type socket results in EINVAL (IPv6)");
}
ATF_TC_BODY(s_negative_udp_socket_v6, tc)
{

	s_negative_udp_socket_test(AF_INET6);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, fd_positive_file_v4);
	ATF_TP_ADD_TC(tp, fd_positive_file_v6);
	ATF_TP_ADD_TC(tp, fd_positive_shm_v4);
	ATF_TP_ADD_TC(tp, fd_positive_shm_v6);
	ATF_TP_ADD_TC(tp, fd_negative_bad_fd_v4);
	ATF_TP_ADD_TC(tp, fd_negative_bad_fd_v6);
	ATF_TP_ADD_TC(tp, flags_v4);
	ATF_TP_ADD_TC(tp, flags_v6);
	/*
	 * TODO: the negative case for SF_NODISKIO (returns EBUSY if file in
	 * use) is not covered yet.
	 *
	 * Need to lock a file in a subprocess in write mode, then try and
	 * send the data in read mode with sendfile.
	 *
	 * This should work with FFS/UFS, but there are no guarantees about
	 * other filesystem implementations of sendfile(2), e.g., ZFS.
	 */
	ATF_TP_ADD_TC(tp, hdtr_positive_v4);
	ATF_TP_ADD_TC(tp, hdtr_positive_v6);
	ATF_TP_ADD_TC(tp, hdtr_negative_bad_pointers_v4);
	ATF_TP_ADD_TC(tp, hdtr_negative_bad_pointers_v6);
	ATF_TP_ADD_TC(tp, offset_negative_value_less_than_zero_v4);
	ATF_TP_ADD_TC(tp, offset_negative_value_less_than_zero_v6);
	ATF_TP_ADD_TC(tp, sbytes_positive_v4);
	ATF_TP_ADD_TC(tp, sbytes_positive_v6);
	ATF_TP_ADD_TC(tp, sbytes_negative_v4);
	ATF_TP_ADD_TC(tp, sbytes_negative_v6);
	ATF_TP_ADD_TC(tp, s_negative_not_connected_socket_v4);
	ATF_TP_ADD_TC(tp, s_negative_not_connected_socket_v6);
	ATF_TP_ADD_TC(tp, s_negative_not_descriptor);
	ATF_TP_ADD_TC(tp, s_negative_not_socket_file_descriptor);
	ATF_TP_ADD_TC(tp, s_negative_udp_socket_v4);
	ATF_TP_ADD_TC(tp, s_negative_udp_socket_v6);

	return (atf_no_error());
}
