/*-
 * Copyright (c) 2018 Aniket Pandey
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
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/un.h>

#include <atf-c.h>
#include <fcntl.h>
#include <stdarg.h>
#include <unistd.h>

#include "utils.h"

#define MAX_DATA 128
#define SERVER_PATH "server"

static pid_t pid;
static mode_t mode = 0777;
static int sockfd, sockfd2, connectfd;
static ssize_t data_bytes;
static socklen_t len = sizeof(struct sockaddr_un);
static struct iovec io1, io2;
static struct pollfd fds[1];
static struct sockaddr_un server;
static struct msghdr sendbuf, recvbuf;
static char extregex[MAX_DATA];
static char data[MAX_DATA];
static char msgbuff[MAX_DATA] = "This message does not exist";
static const char *auclass = "nt";
static const char *path = "fileforaudit";
static const char *nosupregex = "return,failure : Address family "
				"not supported by protocol family";
static const char *invalregex = "return,failure : Bad file descriptor";

/*
 * Initialize iovec structure to be used as a field of struct msghdr
 */
static void
init_iov(struct iovec *io, char msgbuf[], int datalen)
{
	io->iov_base = msgbuf;
	io->iov_len = datalen;
}

/*
 * Initialize msghdr structure for communication via datagram sockets
 */
static void
init_msghdr(struct msghdr *hdrbuf, struct iovec *io, struct sockaddr_un *addr)
{
	socklen_t length;

	bzero(hdrbuf, sizeof(*hdrbuf));
	length = (socklen_t)sizeof(struct sockaddr_un);
	hdrbuf->msg_name = addr;
	hdrbuf->msg_namelen = length;
	hdrbuf->msg_iov = io;
	hdrbuf->msg_iovlen = 1;
}

/*
 * Variadic function to close socket descriptors
 */
static void
close_sockets(int count, ...)
{
	int sockd;
	va_list socklist;
	va_start(socklist, count);
	for (sockd = 0; sockd < count; sockd++) {
		close(va_arg(socklist, int));
	}
	va_end(socklist);
}

/*
 * Assign local filesystem address to a Unix domain socket
 */
static void
assign_address(struct sockaddr_un *serveraddr)
{
	memset(serveraddr, 0, sizeof(*serveraddr));
	serveraddr->sun_family = AF_UNIX;
	strcpy(serveraddr->sun_path, SERVER_PATH);
}


ATF_TC_WITH_CLEANUP(socket_success);
ATF_TC_HEAD(socket_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"socket(2) call");
}

ATF_TC_BODY(socket_success, tc)
{
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE((sockfd = socket(PF_UNIX, SOCK_STREAM, 0)) != -1);
	/* Check the presence of sockfd in audit record */
	snprintf(extregex, sizeof(extregex), "socket.*ret.*success,%d", sockfd);
	check_audit(fds, extregex, pipefd);
	close(sockfd);
}

ATF_TC_CLEANUP(socket_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(socket_failure);
ATF_TC_HEAD(socket_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"socket(2) call");
}

ATF_TC_BODY(socket_failure, tc)
{
	snprintf(extregex, sizeof(extregex), "socket.*%s", nosupregex);
	FILE *pipefd = setup(fds, auclass);
	/* Failure reason: Unsupported value of 'domain' argument: 0 */
	ATF_REQUIRE_EQ(-1, socket(0, SOCK_STREAM, 0));
	check_audit(fds, extregex, pipefd);
}

ATF_TC_CLEANUP(socket_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(socketpair_success);
ATF_TC_HEAD(socketpair_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"socketpair(2) call");
}

ATF_TC_BODY(socketpair_success, tc)
{
	int sv[2];
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, socketpair(PF_UNIX, SOCK_STREAM, 0, sv));

	/* Check for 0x0 (argument 3: default protocol) in the audit record */
	snprintf(extregex, sizeof(extregex), "socketpair.*0x0.*return,success");
	check_audit(fds, extregex, pipefd);
	close_sockets(2, sv[0], sv[1]);
}

ATF_TC_CLEANUP(socketpair_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(socketpair_failure);
ATF_TC_HEAD(socketpair_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"socketpair(2) call");
}

ATF_TC_BODY(socketpair_failure, tc)
{
	snprintf(extregex, sizeof(extregex), "socketpair.*%s", nosupregex);
	FILE *pipefd = setup(fds, auclass);
	/* Failure reason: Unsupported value of 'domain' argument: 0 */
	ATF_REQUIRE_EQ(-1, socketpair(0, SOCK_STREAM, 0, NULL));
	check_audit(fds, extregex, pipefd);
}

ATF_TC_CLEANUP(socketpair_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(setsockopt_success);
ATF_TC_HEAD(setsockopt_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"setsockopt(2) call");
}

ATF_TC_BODY(setsockopt_success, tc)
{
	int tr = 1;
	ATF_REQUIRE((sockfd = socket(PF_UNIX, SOCK_STREAM, 0)) != -1);
	/* Check the presence of sockfd in audit record */
	snprintf(extregex, sizeof(extregex),
			"setsockopt.*0x%x.*return,success", sockfd);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, setsockopt(sockfd, SOL_SOCKET,
		SO_REUSEADDR, &tr, sizeof(int)));
	check_audit(fds, extregex, pipefd);
	close(sockfd);
}

ATF_TC_CLEANUP(setsockopt_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(setsockopt_failure);
ATF_TC_HEAD(setsockopt_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"setsockopt(2) call");
}

ATF_TC_BODY(setsockopt_failure, tc)
{
	snprintf(extregex, sizeof(extregex), "setsockopt.*%s", invalregex);
	FILE *pipefd = setup(fds, auclass);
	/* Failure reason: Invalid socket descriptor */
	ATF_REQUIRE_EQ(-1, setsockopt(-1, SOL_SOCKET, 0, NULL, 0));
	check_audit(fds, extregex, pipefd);
}

ATF_TC_CLEANUP(setsockopt_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(bind_success);
ATF_TC_HEAD(bind_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"bind(2) call");
}

ATF_TC_BODY(bind_success, tc)
{
	assign_address(&server);
	/* Preliminary socket setup */
	ATF_REQUIRE((sockfd = socket(PF_UNIX, SOCK_STREAM, 0)) != -1);
	/* Check the presence of AF_UNIX address path in audit record */
	snprintf(extregex, sizeof(extregex),
		"bind.*unix.*%s.*return,success", SERVER_PATH);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, bind(sockfd, (struct sockaddr *)&server, len));
	check_audit(fds, extregex, pipefd);
	close(sockfd);
}

ATF_TC_CLEANUP(bind_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(bind_failure);
ATF_TC_HEAD(bind_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"bind(2) call");
}

ATF_TC_BODY(bind_failure, tc)
{
	assign_address(&server);
	/* Check the presence of AF_UNIX path in audit record */
	snprintf(extregex, sizeof(extregex),
			"bind.*%s.*return,failure", SERVER_PATH);

	FILE *pipefd = setup(fds, auclass);
	/* Failure reason: Invalid socket descriptor */
	ATF_REQUIRE_EQ(-1, bind(0, (struct sockaddr *)&server, len));
	check_audit(fds, extregex, pipefd);
}

ATF_TC_CLEANUP(bind_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(bindat_success);
ATF_TC_HEAD(bindat_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"bindat(2) call");
}

ATF_TC_BODY(bindat_success, tc)
{
	assign_address(&server);
	/* Preliminary socket setup */
	ATF_REQUIRE((sockfd = socket(PF_UNIX, SOCK_STREAM, 0)) != -1);
	/* Check the presence of socket descriptor in audit record */
	snprintf(extregex, sizeof(extregex),
			"bindat.*0x%x.*return,success", sockfd);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, bindat(AT_FDCWD, sockfd,
			(struct sockaddr *)&server, len));
	check_audit(fds, extregex, pipefd);
	close(sockfd);
}

ATF_TC_CLEANUP(bindat_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(bindat_failure);
ATF_TC_HEAD(bindat_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"bindat(2) call");
}

ATF_TC_BODY(bindat_failure, tc)
{
	assign_address(&server);
	snprintf(extregex, sizeof(extregex), "bindat.*%s", invalregex);

	FILE *pipefd = setup(fds, auclass);
	/* Failure reason: Invalid socket descriptor */
	ATF_REQUIRE_EQ(-1, bindat(AT_FDCWD, -1,
			(struct sockaddr *)&server, len));
	check_audit(fds, extregex, pipefd);
}

ATF_TC_CLEANUP(bindat_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(listen_success);
ATF_TC_HEAD(listen_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"listen(2) call");
}

ATF_TC_BODY(listen_success, tc)
{
	assign_address(&server);
	/* Preliminary socket setup */
	ATF_REQUIRE((sockfd = socket(PF_UNIX, SOCK_STREAM, 0)) != -1);
	ATF_REQUIRE_EQ(0, bind(sockfd, (struct sockaddr *)&server, len));
	/* Check the presence of socket descriptor in the audit record */
	snprintf(extregex, sizeof(extregex),
			"listen.*0x%x.*return,success", sockfd);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, listen(sockfd, 1));
	check_audit(fds, extregex, pipefd);
	close(sockfd);
}

ATF_TC_CLEANUP(listen_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(listen_failure);
ATF_TC_HEAD(listen_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"listen(2) call");
}

ATF_TC_BODY(listen_failure, tc)
{
	snprintf(extregex, sizeof(extregex), "listen.*%s", invalregex);
	FILE *pipefd = setup(fds, auclass);
	/* Failure reason: Invalid socket descriptor */
	ATF_REQUIRE_EQ(-1, listen(-1, 1));
	check_audit(fds, extregex, pipefd);
}

ATF_TC_CLEANUP(listen_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(connect_success);
ATF_TC_HEAD(connect_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"connect(2) call");
}

ATF_TC_BODY(connect_success, tc)
{
	assign_address(&server);
	/* Setup a server socket and bind to the specified address */
	ATF_REQUIRE((sockfd = socket(PF_UNIX, SOCK_STREAM, 0)) != -1);
	ATF_REQUIRE_EQ(0, bind(sockfd, (struct sockaddr *)&server, len));
	ATF_REQUIRE_EQ(0, listen(sockfd, 1));

	/* Set up "blocking" client socket */
	ATF_REQUIRE((sockfd2 = socket(PF_UNIX, SOCK_STREAM, 0)) != -1);

	/* Audit record must contain AF_UNIX address path & sockfd2 */
	snprintf(extregex, sizeof(extregex),
			"connect.*0x%x.*%s.*success", sockfd2, SERVER_PATH);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, connect(sockfd2, (struct sockaddr *)&server, len));
	check_audit(fds, extregex, pipefd);

	/* Close all socket descriptors */
	close_sockets(2, sockfd, sockfd2);
}

ATF_TC_CLEANUP(connect_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(connect_failure);
ATF_TC_HEAD(connect_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"connect(2) call");
}

ATF_TC_BODY(connect_failure, tc)
{
	assign_address(&server);
	/* Audit record must contain AF_UNIX address path */
	snprintf(extregex, sizeof(extregex),
			"connect.*%s.*return,failure", SERVER_PATH);

	FILE *pipefd = setup(fds, auclass);
	/* Failure reason: Invalid socket descriptor */
	ATF_REQUIRE_EQ(-1, connect(-1, (struct sockaddr *)&server, len));
	check_audit(fds, extregex, pipefd);
}

ATF_TC_CLEANUP(connect_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(connectat_success);
ATF_TC_HEAD(connectat_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"connectat(2) call");
}

ATF_TC_BODY(connectat_success, tc)
{
	assign_address(&server);
	/* Setup a server socket and bind to the specified address */
	ATF_REQUIRE((sockfd = socket(PF_UNIX, SOCK_STREAM, 0)) != -1);
	ATF_REQUIRE_EQ(0, bind(sockfd, (struct sockaddr *)&server, len));
	ATF_REQUIRE_EQ(0, listen(sockfd, 1));

	/* Set up "blocking" client socket */
	ATF_REQUIRE((sockfd2 = socket(PF_UNIX, SOCK_STREAM, 0)) != -1);

	/* Audit record must contain sockfd2 */
	snprintf(extregex, sizeof(extregex),
			"connectat.*0x%x.*return,success", sockfd2);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, connectat(AT_FDCWD, sockfd2,
			(struct sockaddr *)&server, len));
	check_audit(fds, extregex, pipefd);

	/* Close all socket descriptors */
	close_sockets(2, sockfd, sockfd2);
}

ATF_TC_CLEANUP(connectat_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(connectat_failure);
ATF_TC_HEAD(connectat_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"connectat(2) call");
}

ATF_TC_BODY(connectat_failure, tc)
{
	assign_address(&server);
	snprintf(extregex, sizeof(extregex), "connectat.*%s", invalregex);

	FILE *pipefd = setup(fds, auclass);
	/* Failure reason: Invalid socket descriptor */
	ATF_REQUIRE_EQ(-1, connectat(AT_FDCWD, -1,
			(struct sockaddr *)&server, len));
	check_audit(fds, extregex, pipefd);
}

ATF_TC_CLEANUP(connectat_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(accept_success);
ATF_TC_HEAD(accept_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"accept(2) call");
}

ATF_TC_BODY(accept_success, tc)
{
	assign_address(&server);
	/* Setup a server socket and bind to the specified address */
	ATF_REQUIRE((sockfd = socket(PF_UNIX, SOCK_STREAM, 0)) != -1);
	ATF_REQUIRE_EQ(0, bind(sockfd, (struct sockaddr *)&server, len));
	ATF_REQUIRE_EQ(0, listen(sockfd, 1));

	/* Set up "blocking" client socket */
	ATF_REQUIRE((sockfd2 = socket(PF_UNIX, SOCK_STREAM, 0)) != -1);
	ATF_REQUIRE_EQ(0, connect(sockfd2, (struct sockaddr *)&server, len));

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE((connectfd = accept(sockfd, NULL, &len)) != -1);

	/* Audit record must contain connectfd & sockfd */
	snprintf(extregex, sizeof(extregex),
			"accept.*0x%x.*return,success,%d", sockfd, connectfd);
	check_audit(fds, extregex, pipefd);

	/* Close all socket descriptors */
	close_sockets(3, sockfd, sockfd2, connectfd);
}

ATF_TC_CLEANUP(accept_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(accept_failure);
ATF_TC_HEAD(accept_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"accept(2) call");
}

ATF_TC_BODY(accept_failure, tc)
{
	snprintf(extregex, sizeof(extregex), "accept.*%s", invalregex);
	FILE *pipefd = setup(fds, auclass);
	/* Failure reason: Invalid socket descriptor */
	ATF_REQUIRE_EQ(-1, accept(-1, NULL, NULL));
	check_audit(fds, extregex, pipefd);
}

ATF_TC_CLEANUP(accept_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(send_success);
ATF_TC_HEAD(send_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"send(2) call");
}

ATF_TC_BODY(send_success, tc)
{
	assign_address(&server);
	/* Setup a server socket and bind to the specified address */
	ATF_REQUIRE((sockfd = socket(PF_UNIX, SOCK_STREAM, 0)) != -1);
	ATF_REQUIRE_EQ(0, bind(sockfd, (struct sockaddr *)&server, len));
	ATF_REQUIRE_EQ(0, listen(sockfd, 1));

	/* Set up "blocking" client and connect with non-blocking server */
	ATF_REQUIRE((sockfd2 = socket(PF_UNIX, SOCK_STREAM, 0)) != -1);
	ATF_REQUIRE_EQ(0, connect(sockfd2, (struct sockaddr *)&server, len));
	ATF_REQUIRE((connectfd = accept(sockfd, NULL, &len)) != -1);

	/* Send a sample message to the connected socket */
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE((data_bytes =
		send(sockfd2, msgbuff, strlen(msgbuff), 0)) != -1);

	/* Audit record must contain sockfd2 and data_bytes */
	snprintf(extregex, sizeof(extregex),
		"send.*0x%x.*return,success,%zd", sockfd2, data_bytes);
	check_audit(fds, extregex, pipefd);

	/* Close all socket descriptors */
	close_sockets(3, sockfd, sockfd2, connectfd);
}

ATF_TC_CLEANUP(send_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(send_failure);
ATF_TC_HEAD(send_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"send(2) call");
}

ATF_TC_BODY(send_failure, tc)
{
	snprintf(extregex, sizeof(extregex), "send.*%s", invalregex);
	FILE *pipefd = setup(fds, auclass);
	/* Failure reason: Invalid socket descriptor */
	ATF_REQUIRE_EQ(-1, send(-1, NULL, 0, 0));
	check_audit(fds, extregex, pipefd);
}

ATF_TC_CLEANUP(send_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(recv_success);
ATF_TC_HEAD(recv_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"recv(2) call");
}

ATF_TC_BODY(recv_success, tc)
{
	assign_address(&server);
	/* Setup a server socket and bind to the specified address */
	ATF_REQUIRE((sockfd = socket(PF_UNIX, SOCK_STREAM, 0)) != -1);
	ATF_REQUIRE_EQ(0, bind(sockfd, (struct sockaddr *)&server, len));
	ATF_REQUIRE_EQ(0, listen(sockfd, 1));

	/* Set up "blocking" client and connect with non-blocking server */
	ATF_REQUIRE((sockfd2 = socket(PF_UNIX, SOCK_STREAM, 0)) != -1);
	ATF_REQUIRE_EQ(0, connect(sockfd2, (struct sockaddr *)&server, len));
	ATF_REQUIRE((connectfd = accept(sockfd, NULL, &len)) != -1);
	/* Send a sample message to the connected socket */
	ATF_REQUIRE(send(sockfd2, msgbuff, strlen(msgbuff), 0) != -1);

	/* Receive data once connectfd is ready for reading */
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE((data_bytes = recv(connectfd, data, MAX_DATA, 0)) != 0);

	/* Audit record must contain connectfd and data_bytes */
	snprintf(extregex, sizeof(extregex),
		"recv.*0x%x.*return,success,%zd", connectfd, data_bytes);
	check_audit(fds, extregex, pipefd);

	/* Close all socket descriptors */
	close_sockets(3, sockfd, sockfd2, connectfd);
}

ATF_TC_CLEANUP(recv_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(recv_failure);
ATF_TC_HEAD(recv_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"recv(2) call");
}

ATF_TC_BODY(recv_failure, tc)
{
	snprintf(extregex, sizeof(extregex), "recv.*%s", invalregex);
	FILE *pipefd = setup(fds, auclass);
	/* Failure reason: Invalid socket descriptor */
	ATF_REQUIRE_EQ(-1, recv(-1, NULL, 0, 0));
	check_audit(fds, extregex, pipefd);
}

ATF_TC_CLEANUP(recv_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(sendto_success);
ATF_TC_HEAD(sendto_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"sendto(2) call");
}

ATF_TC_BODY(sendto_success, tc)
{
	assign_address(&server);
	/*  Setup a server socket and bind to the specified address */
	ATF_REQUIRE((sockfd = socket(PF_UNIX, SOCK_DGRAM, 0)) != -1);
	ATF_REQUIRE_EQ(0, bind(sockfd, (struct sockaddr *)&server, len));

	/* Set up client socket to be used for sending the data */
	ATF_REQUIRE((sockfd2 = socket(PF_UNIX, SOCK_DGRAM, 0)) != -1);

	/* Send a sample message to server's address */
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE((data_bytes = sendto(sockfd2, msgbuff,
		strlen(msgbuff), 0, (struct sockaddr *)&server, len)) != -1);

	/* Audit record must contain sockfd2 and data_bytes */
	snprintf(extregex, sizeof(extregex),
		"sendto.*0x%x.*return,success,%zd", sockfd2, data_bytes);
	check_audit(fds, extregex, pipefd);

	/* Close all socket descriptors */
	close_sockets(2, sockfd, sockfd2);
}

ATF_TC_CLEANUP(sendto_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(sendto_failure);
ATF_TC_HEAD(sendto_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"sendto(2) call");
}

ATF_TC_BODY(sendto_failure, tc)
{
	snprintf(extregex, sizeof(extregex), "sendto.*%s", invalregex);
	FILE *pipefd = setup(fds, auclass);
	/* Failure reason: Invalid socket descriptor */
	ATF_REQUIRE_EQ(-1, sendto(-1, NULL, 0, 0, NULL, 0));
	check_audit(fds, extregex, pipefd);
}

ATF_TC_CLEANUP(sendto_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(recvfrom_success);
ATF_TC_HEAD(recvfrom_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"recvfrom(2) call");
}

ATF_TC_BODY(recvfrom_success, tc)
{
	assign_address(&server);
	/*  Setup a server socket and bind to the specified address */
	ATF_REQUIRE((sockfd = socket(PF_UNIX, SOCK_DGRAM, 0)) != -1);
	ATF_REQUIRE_EQ(0, bind(sockfd, (struct sockaddr *)&server, len));

	/* Set up client socket to be used for sending the data */
	ATF_REQUIRE((sockfd2 = socket(PF_UNIX, SOCK_DGRAM, 0)) != -1);
	ATF_REQUIRE(sendto(sockfd2, msgbuff, strlen(msgbuff), 0,
		(struct sockaddr *)&server, len) != -1);

	/* Receive data once sockfd is ready for reading */
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE((data_bytes = recvfrom(sockfd, data,
		MAX_DATA, 0, NULL, &len)) != 0);

	/* Audit record must contain sockfd and data_bytes */
	snprintf(extregex, sizeof(extregex),
		"recvfrom.*0x%x.*return,success,%zd", sockfd, data_bytes);
	check_audit(fds, extregex, pipefd);

	/* Close all socket descriptors */
	close_sockets(2, sockfd, sockfd2);
}

ATF_TC_CLEANUP(recvfrom_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(recvfrom_failure);
ATF_TC_HEAD(recvfrom_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"recvfrom(2) call");
}

ATF_TC_BODY(recvfrom_failure, tc)
{
	snprintf(extregex, sizeof(extregex), "recvfrom.*%s", invalregex);
	FILE *pipefd = setup(fds, auclass);
	/* Failure reason: Invalid socket descriptor */
	ATF_REQUIRE_EQ(-1, recvfrom(-1, NULL, 0, 0, NULL, NULL));
	check_audit(fds, extregex, pipefd);
}

ATF_TC_CLEANUP(recvfrom_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(sendmsg_success);
ATF_TC_HEAD(sendmsg_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"recvmsg(2) call");
}

ATF_TC_BODY(sendmsg_success, tc)
{
	assign_address(&server);
	/* Create a datagram server socket & bind to UNIX address family */
	ATF_REQUIRE((sockfd = socket(PF_UNIX, SOCK_DGRAM, 0)) != -1);
	ATF_REQUIRE_EQ(0, bind(sockfd, (struct sockaddr *)&server, len));

	/* Message buffer to be sent to the server */
	init_iov(&io1, msgbuff, sizeof(msgbuff));
	init_msghdr(&sendbuf, &io1, &server);

	/* Set up UDP client to communicate with the server */
	ATF_REQUIRE((sockfd2 = socket(PF_UNIX, SOCK_DGRAM, 0)) != -1);

	/* Send a sample message to the specified client address */
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE((data_bytes = sendmsg(sockfd2, &sendbuf, 0)) != -1);

	/* Audit record must contain sockfd2 and data_bytes */
	snprintf(extregex, sizeof(extregex),
		"sendmsg.*0x%x.*return,success,%zd", sockfd2, data_bytes);
	check_audit(fds, extregex, pipefd);

	/* Close all socket descriptors */
	close_sockets(2, sockfd, sockfd2);
}

ATF_TC_CLEANUP(sendmsg_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(sendmsg_failure);
ATF_TC_HEAD(sendmsg_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"sendmsg(2) call");
}

ATF_TC_BODY(sendmsg_failure, tc)
{
	snprintf(extregex, sizeof(extregex),
		"sendmsg.*return,failure : Bad address");
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(-1, sendmsg(-1, NULL, 0));
	check_audit(fds, extregex, pipefd);
}

ATF_TC_CLEANUP(sendmsg_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(recvmsg_success);
ATF_TC_HEAD(recvmsg_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"recvmsg(2) call");
}

ATF_TC_BODY(recvmsg_success, tc)
{
	assign_address(&server);
	/* Create a datagram server socket & bind to UNIX address family */
	ATF_REQUIRE((sockfd = socket(PF_UNIX, SOCK_DGRAM, 0)) != -1);
	ATF_REQUIRE_EQ(0, bind(sockfd, (struct sockaddr *)&server, len));

	/* Message buffer to be sent to the server */
	init_iov(&io1, msgbuff, sizeof(msgbuff));
	init_msghdr(&sendbuf, &io1, &server);

	/* Prepare buffer to store the received data in */
	init_iov(&io2, data, sizeof(data));
	init_msghdr(&recvbuf, &io2, NULL);

	/* Set up UDP client to communicate with the server */
	ATF_REQUIRE((sockfd2 = socket(PF_UNIX, SOCK_DGRAM, 0)) != -1);
	/* Send a sample message to the connected socket */
	ATF_REQUIRE(sendmsg(sockfd2, &sendbuf, 0) != -1);

	/* Receive data once clientfd is ready for reading */
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE((data_bytes = recvmsg(sockfd, &recvbuf, 0)) != -1);

	/* Audit record must contain sockfd and data_bytes */
	snprintf(extregex, sizeof(extregex),
		"recvmsg.*%#x.*return,success,%zd", sockfd, data_bytes);
	check_audit(fds, extregex, pipefd);

	/* Close all socket descriptors */
	close_sockets(2, sockfd, sockfd2);
}

ATF_TC_CLEANUP(recvmsg_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(recvmsg_failure);
ATF_TC_HEAD(recvmsg_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"recvmsg(2) call");
}

ATF_TC_BODY(recvmsg_failure, tc)
{
	snprintf(extregex, sizeof(extregex),
		"recvmsg.*return,failure : Bad address");
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(-1, recvmsg(-1, NULL, 0));
	check_audit(fds, extregex, pipefd);
}

ATF_TC_CLEANUP(recvmsg_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(shutdown_success);
ATF_TC_HEAD(shutdown_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"shutdown(2) call");
}

ATF_TC_BODY(shutdown_success, tc)
{
	assign_address(&server);
	/* Setup server socket and bind to the specified address */
	ATF_REQUIRE((sockfd = socket(PF_UNIX, SOCK_STREAM, 0)) != -1);
	ATF_REQUIRE_EQ(0, bind(sockfd, (struct sockaddr *)&server, len));
	ATF_REQUIRE_EQ(0, listen(sockfd, 1));

	/* Setup client and connect with the blocking server */
	ATF_REQUIRE((sockfd2 = socket(PF_UNIX, SOCK_STREAM, 0)) != -1);
	ATF_REQUIRE_EQ(0, connect(sockfd2, (struct sockaddr *)&server, len));
	ATF_REQUIRE((connectfd = accept(sockfd, NULL, &len)) != -1);

	/* Audit record must contain clientfd */
	snprintf(extregex, sizeof(extregex),
		"shutdown.*%#x.*return,success", connectfd);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, shutdown(connectfd, SHUT_RDWR));
	check_audit(fds, extregex, pipefd);

	/* Close all socket descriptors */
	close_sockets(3, sockfd, sockfd2, connectfd);
}

ATF_TC_CLEANUP(shutdown_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(shutdown_failure);
ATF_TC_HEAD(shutdown_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"shutdown(2) call");
}

ATF_TC_BODY(shutdown_failure, tc)
{
	pid = getpid();
	snprintf(extregex, sizeof(extregex),
		"shutdown.*%d.*return,failure", pid);

	FILE *pipefd = setup(fds, auclass);
	/* Failure reason: Invalid socket descriptor */
	ATF_REQUIRE_EQ(-1, shutdown(-1, SHUT_RDWR));
	check_audit(fds, extregex, pipefd);
}

ATF_TC_CLEANUP(shutdown_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(sendfile_success);
ATF_TC_HEAD(sendfile_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"sendfile(2) call");
}

ATF_TC_BODY(sendfile_success, tc)
{
	int filedesc;
	ATF_REQUIRE((filedesc = open(path, O_CREAT | O_RDONLY, mode)) != -1);
	/* Create a simple UNIX socket to send out random data */
	ATF_REQUIRE((sockfd = socket(PF_UNIX, SOCK_STREAM, 0)) != -1);
	/* Check the presence of sockfd, non-file in the audit record */
	snprintf(extregex, sizeof(extregex),
		"sendfile.*%#x,non-file.*return,success", filedesc);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, sendfile(filedesc, sockfd, 0, 0, NULL, NULL, 0));
	check_audit(fds, extregex, pipefd);

	/* Teardown socket and file descriptors */
	close_sockets(2, sockfd, filedesc);
}

ATF_TC_CLEANUP(sendfile_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(sendfile_failure);
ATF_TC_HEAD(sendfile_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"sendfile(2) call");
}

ATF_TC_BODY(sendfile_failure, tc)
{
	pid = getpid();
	snprintf(extregex, sizeof(extregex),
		"sendfile.*%d.*return,failure", pid);
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(-1, sendfile(-1, -1, 0, 0, NULL, NULL, 0));
	check_audit(fds, extregex, pipefd);
}

ATF_TC_CLEANUP(sendfile_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(setfib_success);
ATF_TC_HEAD(setfib_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"setfib(2) call");
}

ATF_TC_BODY(setfib_success, tc)
{
	pid = getpid();
	snprintf(extregex, sizeof(extregex), "setfib.*%d.*return,success", pid);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, setfib(0));
	check_audit(fds, extregex, pipefd);
}

ATF_TC_CLEANUP(setfib_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(setfib_failure);
ATF_TC_HEAD(setfib_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"setfib(2) call");
}

ATF_TC_BODY(setfib_failure, tc)
{
	pid = getpid();
	snprintf(extregex, sizeof(extregex), "setfib.*%d.*return,failure", pid);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(-1, setfib(-1));
	check_audit(fds, extregex, pipefd);
}

ATF_TC_CLEANUP(setfib_failure, tc)
{
	cleanup();
}


ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, socket_success);
	ATF_TP_ADD_TC(tp, socket_failure);
	ATF_TP_ADD_TC(tp, socketpair_success);
	ATF_TP_ADD_TC(tp, socketpair_failure);
	ATF_TP_ADD_TC(tp, setsockopt_success);
	ATF_TP_ADD_TC(tp, setsockopt_failure);

	ATF_TP_ADD_TC(tp, bind_success);
	ATF_TP_ADD_TC(tp, bind_failure);
	ATF_TP_ADD_TC(tp, bindat_success);
	ATF_TP_ADD_TC(tp, bindat_failure);
	ATF_TP_ADD_TC(tp, listen_success);
	ATF_TP_ADD_TC(tp, listen_failure);

	ATF_TP_ADD_TC(tp, connect_success);
	ATF_TP_ADD_TC(tp, connect_failure);
	ATF_TP_ADD_TC(tp, connectat_success);
	ATF_TP_ADD_TC(tp, connectat_failure);
	ATF_TP_ADD_TC(tp, accept_success);
	ATF_TP_ADD_TC(tp, accept_failure);

	ATF_TP_ADD_TC(tp, send_success);
	ATF_TP_ADD_TC(tp, send_failure);
	ATF_TP_ADD_TC(tp, recv_success);
	ATF_TP_ADD_TC(tp, recv_failure);

	ATF_TP_ADD_TC(tp, sendto_success);
	ATF_TP_ADD_TC(tp, sendto_failure);
	ATF_TP_ADD_TC(tp, recvfrom_success);
	ATF_TP_ADD_TC(tp, recvfrom_failure);

	ATF_TP_ADD_TC(tp, sendmsg_success);
	ATF_TP_ADD_TC(tp, sendmsg_failure);
	ATF_TP_ADD_TC(tp, recvmsg_success);
	ATF_TP_ADD_TC(tp, recvmsg_failure);

	ATF_TP_ADD_TC(tp, shutdown_success);
	ATF_TP_ADD_TC(tp, shutdown_failure);
	ATF_TP_ADD_TC(tp, sendfile_success);
	ATF_TP_ADD_TC(tp, sendfile_failure);
	ATF_TP_ADD_TC(tp, setfib_success);
	ATF_TP_ADD_TC(tp, setfib_failure);

	return (atf_no_error());
}
