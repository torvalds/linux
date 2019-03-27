/*-
 * Copyright (c) 2005 Robert N. M. Watson
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
#include <sys/sysctl.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/*
 * This regression test is intended to validate that the backlog parameter
 * set by listen() is properly set, can be retrieved using SO_LISTENQLIMIT,
 * and that it can be updated by later calls to listen().  We also check that
 * SO_LISTENQLIMIT cannot be set.
 *
 * Future things to test:
 *
 * - That if we change the value of kern.ipc.somaxconn, the limits really
 *   do change.
 *
 * - That limits are, approximately, enforced and implemented.
 *
 * - All this on multiple socket types -- i.e., PF_LOCAL.
 *
 * - That we also test SO_LISTENQLEN and SO_LISTENINCQLEN.
 */

/*
 * We retrieve kern.ipc.somaxconn before running the tests in order to use a
 * run-time set value of SOMAXCONN, rather than compile-time set.  We assume
 * that no other process will be simultaneously frobbing it, and these tests
 * may fail if that assumption is not held.
 */
static int	somaxconn;

/*
 * Retrieve the current socket listen queue limit using SO_LISTENQLIMIT.
 */
static int
socket_get_backlog(int sock, int *backlogp, const char *testclass,
    const char *test, const char *testfunc)
{
	socklen_t len;
	int i;

	len = sizeof(i);
	if (getsockopt(sock, SOL_SOCKET, SO_LISTENQLIMIT, &i, &len) < 0) {
		warn("%s: %s: %s: socket_get_backlog: getsockopt("
		    "SOL_SOCKET, SO_LISTENQLIMIT)", testclass, test,
		    testfunc);
		return (-1);
	}

	if (len != sizeof(i)) {
		warnx("%s: %s: %s: socket_get_backlog: getsockopt("
		    "SOL_SOCKET, SO_LISTENQLIMIT): returned size %d",
		    testclass, test, testfunc, len);
		return (-1);
	}

	*backlogp = i;

	return (0);
}

/*
 * Create a socket, check the queue limit on creation, perform a listen(),
 * and make sure that the limit was set as expected by listen().
 */
static int
socket_listen(int domain, int type, int protocol, int backlog,
    int create_backlog_assertion, int listen_backlog_assertion, int *sockp,
    const char *domainstring, const char *typestring, const char *testclass,
    const char *test)
{
	int backlog_retrieved, sock;

	sock = socket(domain, type, protocol);
	if (sock < 0) {
		warn("%s: %s: socket_listen: socket(%s, %s)", testclass,
		    test, domainstring, typestring);
		close(sock);
		return (-1);
	}

	if (socket_get_backlog(sock, &backlog_retrieved, testclass, test,
	    "socket_listen") < 0) {
		close(sock);
		return (-1);
	}

	if (backlog_retrieved != create_backlog_assertion) {
		warnx("%s: %s: socket_listen: create backlog is %d not %d",
		    testclass, test, backlog_retrieved,
		    create_backlog_assertion);
		close(sock);
		return (-1);
	}

	if (listen(sock, backlog) < 0) {
		warn("%s: %s: socket_listen: listen(, %d)", testclass, test,
		    backlog);
		close(sock);
		return (-1);
	}

	if (socket_get_backlog(sock, &backlog_retrieved, testclass, test,
	    "socket_listen") < 0) {
		close(sock);
		return (-1);
	}

	if (backlog_retrieved != listen_backlog_assertion) {
		warnx("%s: %s: socket_listen: listen backlog is %d not %d",
		    testclass, test, backlog_retrieved,
		    listen_backlog_assertion);
		close(sock);
		return (-1);
	}

	*sockp = sock;
	return (0);
}

/*
 * This test creates sockets and tests default states before and after
 * listen().  Specifically, we expect a queue limit of 0 before listen, and
 * then various settings for after listen().  If the passed backlog was
 * either < 0 or > somaxconn, it should be set to somaxconn; otherwise, the
 * passed queue depth.
 */
static void
test_defaults(void)
{
	int sock;

	/*
	 * First pass.  Confirm the default is 0.  Listen with a backlog of
	 * 0 and confirm it gets set that way.
	 */
	if (socket_listen(PF_INET, SOCK_STREAM, 0, 0, 0, 0, &sock, "PF_INET",
	    "SOCK_STREAM", "test_defaults", "default_0_listen_0") < 0)
		exit(-1);
	close(sock);

	/*
	 * Second pass.  Listen with a backlog of -1 and make sure it is set
	 * to somaxconn.
	 */
	if (socket_listen(PF_INET, SOCK_STREAM, 0, -1, 0, somaxconn, &sock,
	    "PF_INET", "SOCK_STREAM", "test_defaults", "default_0_listen_-1")
	    < 0)
		exit(-1);
	close(sock);

	/*
	 * Third pass.  Listen with a backlog of 1 and make sure it is set to
	 * 1.
	 */
	if (socket_listen(PF_INET, SOCK_STREAM, 0, 1, 0, 1, &sock, "PF_INET",
	    "SOCK_STREAM", "test_defaults", "default_0_listen_1") < 0)
		exit(-1);
	close(sock);

	/*
	 * Fourth pass.  Listen with a backlog of somaxconn and make sure it
	 * is set to somaxconn.
	 */
	if (socket_listen(PF_INET, SOCK_STREAM, 0, somaxconn, 0, somaxconn,
	    &sock, "PF_INET", "SOCK_STREAM", "test_defaults",
	    "default_0_listen_somaxconn") < 0)
		exit(-1);
	close(sock);

	/*
	 * Fifth pass.  Listen with a backlog of somaxconn+1 and make sure it
	 * is set to somaxconn.
	 */
	if (socket_listen(PF_INET, SOCK_STREAM, 0, somaxconn+1, 0, somaxconn,
	    &sock, "PF_INET", "SOCK_STREAM", "test_defaults",
	    "default_0_listen_somaxconn+1") < 0)
		exit(-1);
	close(sock);
}

/*
 * Create a socket, set the initial listen() state, then update the queue
 * depth using listen().  Check that the backlog is as expected after both
 * the first and second listen().
 */
static int
socket_listen_update(int domain __unused, int type __unused,
    int protocol __unused, int backlog,
    int update_backlog, int listen_backlog_assertion,
    int update_backlog_assertion, int *sockp, const char *domainstring,
    const char *typestring, const char *testclass, const char *test)
{
	int backlog_retrieved, sock;

	sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		warn("%s: %s: socket_listen_update: socket(%s, %s)",
		    testclass, test, domainstring, typestring);
		return (-1);
	}

	if (listen(sock, backlog) < 0) {
		warn("%s: %s: socket_listen_update: initial listen(, %d)",
		    testclass, test, backlog);
		close(sock);
		return (-1);
	}

	if (socket_get_backlog(sock, &backlog_retrieved, testclass, test,
	    "socket_listen_update") < 0) {
		close(sock);
		return (-1);
	}

	if (backlog_retrieved != listen_backlog_assertion) {
		warnx("%s: %s: socket_listen_update: initial backlog is %d "
		    "not %d", testclass, test, backlog_retrieved,
		    listen_backlog_assertion);
		close(sock);
		return (-1);
	}

	if (listen(sock, update_backlog) < 0) {
		warn("%s: %s: socket_listen_update: update listen(, %d)",
		    testclass, test, update_backlog);
		close(sock);
		return (-1);
	}

	if (socket_get_backlog(sock, &backlog_retrieved, testclass, test,
	    "socket_listen_update") < 0) {
		close(sock);
		return (-1);
	}

	if (backlog_retrieved != update_backlog_assertion) {
		warnx("%s: %s: socket_listen_update: updated backlog is %d "
		    "not %d", testclass, test, backlog_retrieved,
		    update_backlog_assertion);
		close(sock);
		return (-1);
	}

	*sockp = sock;
	return (0);
}

/*
 * This test tests using listen() to update the queue depth after a socket
 * has already been marked as listening.  We test several cases: setting the
 * socket < 0, 0, 1, somaxconn, and somaxconn + 1.
 */
static void
test_listen_update(void)
{
	int sock;

	/*
	 * Set to 5, update to -1, which should give somaxconn.
	 */
	if (socket_listen_update(PF_INET, SOCK_STREAM, 0, 5, -1, 5, somaxconn,
	    &sock, "PF_INET", "SOCK_STREAM", "test_listen_update",
	    "update_5,-1") < 0)
		exit(-1);
	close(sock);

	/*
	 * Set to 5, update to 0, which should give 0.
	 */
	if (socket_listen_update(PF_INET, SOCK_STREAM, 0, 5, 0, 5, 0, &sock,
	    "PF_INET", "SOCK_STREAM", "test_listen_update", "update_5,0")
	    < 0)
		exit(-1);
	close(sock);

	/*
	 * Set to 5, update to 1, which should give 1.
	 */
	if (socket_listen_update(PF_INET, SOCK_STREAM, 0, 5, 1, 5, 1, &sock,
	    "PF_INET", "SOCK_STREAM", "test_listen_update", "update_5,1")
	    < 0)
		exit(-1);
	close(sock);

	/*
	 * Set to 5, update to somaxconn, which should give somaxconn.
	 */
	if (socket_listen_update(PF_INET, SOCK_STREAM, 0, 5, somaxconn, 5,
	    somaxconn, &sock, "PF_INET", "SOCK_STREAM", "test_listen_update",
	    "update_5,somaxconn") < 0)
		exit(-1);
	close(sock);

	/*
	 * Set to 5, update to somaxconn+1, which should give somaxconn.
	 */
	if (socket_listen_update(PF_INET, SOCK_STREAM, 0, 5, somaxconn+1, 5,
	    somaxconn, &sock, "PF_INET", "SOCK_STREAM", "test_listen_update",
	    "update_5,somaxconn+1") < 0)
		exit(-1);
	close(sock);
}

/*
 * SO_LISTENQLIMIT is a read-only socket option, so make sure we get an error
 * if we try to write it.
 */
static void
test_set_qlimit(void)
{
	int i, ret, sock;

	sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock < 0)
		err(-1, "test_set_qlimit: socket(PF_INET, SOCK_STREAM)");

	i = 0;
	ret = setsockopt(sock, SOL_SOCKET, SO_LISTENQLIMIT, &i, sizeof(i));
	if (ret < 0 && errno != ENOPROTOOPT) {
		warn("test_set_qlimit: setsockopt(SOL_SOCKET, "
		    "SO_LISTENQLIMIT, 0): unexpected error");
		close(sock);
	}

	if (ret == 0) {
		warnx("test_set_qlimit: setsockopt(SOL_SOCKET, "
		    "SO_LISTENQLIMIT, 0) succeeded");
		close(sock);
		exit(-1);
	}
	close(sock);
}

int
main(void)
{
	size_t len;

	len = sizeof(somaxconn);
	if (sysctlbyname("kern.ipc.somaxconn", &somaxconn, &len, NULL, 0)
	    < 0)
		err(-1, "sysctlbyname(kern.ipc.somaxconn)");

	test_defaults();
	test_listen_update();
	test_set_qlimit();

	return (0);
}
