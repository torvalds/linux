/*-
 * Copyright (c) 2007 Robert N. M. Watson
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

/*
 * A few regression tests for UNIX domain sockets.  Run from single-user mode
 * as it checks the openfiles sysctl to look for leaks, and we don't want that
 * changing due to other processes doing stuff.
 */

#include <sys/types.h>
#include <sys/signal.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/un.h>
#include <sys/wait.h>

#include <netinet/in.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int forcegc = 1;
static char dpath[PATH_MAX];
static const char *test;

static int
getsysctl(const char *name)
{
	size_t len;
	int i;

	len = sizeof(i);
	if (sysctlbyname(name, &i, &len, NULL, 0) < 0)
		err(-1, "%s", name);
	return (i);
}

static int
getopenfiles(void)
{

	return (getsysctl("kern.openfiles"));
}

static int
getinflight(void)
{

	return (getsysctl("net.local.inflight"));
}

static int
getdeferred(void)
{

	return (getsysctl("net.local.deferred"));
}

static void
sendfd(int fd, int fdtosend)
{
	struct msghdr mh;
	struct message { struct cmsghdr msg_hdr; int fd; } m;
	ssize_t len;
	int after_inflight, before_inflight;

	before_inflight = getinflight();

	bzero(&mh, sizeof(mh));
	bzero(&m, sizeof(m));
	mh.msg_control = &m;
	mh.msg_controllen = sizeof(m);
	m.msg_hdr.cmsg_len = sizeof(m);
	m.msg_hdr.cmsg_level = SOL_SOCKET;
	m.msg_hdr.cmsg_type = SCM_RIGHTS;
	m.fd = fdtosend;
	len = sendmsg(fd, &mh, 0);
	if (len < 0)
		err(-1, "%s: sendmsg", test);
	after_inflight = getinflight();
	if (after_inflight != before_inflight + 1)
		errx(-1, "%s: sendfd: before %d after %d\n", test,
		    before_inflight, after_inflight);
}

static void
close2(int fd1, int fd2)
{

	close(fd1);
	close(fd2);
}

static void
close3(int fd1, int fd2, int fd3)
{

	close2(fd1, fd2);
	close(fd3);
}

static void
close4(int fd1, int fd2, int fd3, int fd4)
{

	close2(fd1, fd2);
	close2(fd3, fd4);
}

static void
close5(int fd1, int fd2, int fd3, int fd4, int fd5)
{

	close3(fd1, fd2, fd3);
	close2(fd4, fd5);
}

static int
my_socket(int domain, int type, int proto)
{
	int sock;

	sock = socket(domain, type, proto);
	if (sock < 0)
		err(-1, "%s: socket", test);
	return (sock);
}

static void
my_bind(int sock, struct sockaddr *sa, socklen_t len)
{

	if (bind(sock, sa, len) < 0)
		err(-1, "%s: bind", test);
}

static void
my_connect(int sock, struct sockaddr *sa, socklen_t len)
{

	if (connect(sock, sa, len) < 0 && errno != EINPROGRESS)
		err(-1, "%s: connect", test);
}

static void
my_listen(int sock, int backlog)
{

	if (listen(sock, backlog) < 0)
		err(-1, "%s: listen", test);
}

static void
my_socketpair(int *sv)
{

	if (socketpair(PF_UNIX, SOCK_STREAM, 0, sv) < 0)
		err(-1, "%s: socketpair", test);
}

static void
my_getsockname(int s, struct sockaddr *sa, socklen_t *salen)
{

	if (getsockname(s, sa, salen) < 0)
		err(-1, "%s: getsockname", test);
}

static void
setnonblock(int s)
{

	if (fcntl(s, F_SETFL, O_NONBLOCK) < 0)
		err(-1, "%s: fcntl(F_SETFL, O_NONBLOCK)", test);
}

static void
alloc3fds(int *s, int *sv)
{

	if ((*s = socket(PF_UNIX, SOCK_STREAM, 0)) < 0)
		err(-1, "%s: socket", test);
	if (socketpair(PF_UNIX, SOCK_STREAM, 0, sv) < 0)
		err(-1, "%s: socketpair", test);
}

static void
alloc5fds(int *s, int *sva, int *svb)
{

	if ((*s = socket(PF_UNIX, SOCK_STREAM, 0)) < 0)
		err(-1, "%s: socket", test);
	if (socketpair(PF_UNIX, SOCK_STREAM, 0, sva) < 0)
		err(-1, "%s: socketpair", test);
	if (socketpair(PF_UNIX, SOCK_STREAM, 0, svb) < 0)
		err(-1, "%s: socketpair", test);
}

static void
save_sysctls(int *before_inflight, int *before_openfiles)
{

	*before_inflight = getinflight();
	*before_openfiles = getopenfiles();
}

/*
 * Try hard to make sure that the GC does in fact run before we test the
 * condition of things.
 */
static void
trigger_gc(void)
{
	int s;

	if (forcegc) {
		if ((s = socket(PF_UNIX, SOCK_STREAM, 0)) < 0)
			err(-1, "trigger_gc: socket");
		close(s);
	}
	sleep(1);
}

static void
test_sysctls(int before_inflight, int before_openfiles)
{
	int after_inflight, after_openfiles;

	trigger_gc();
	after_inflight = getinflight();
	if (after_inflight != before_inflight)
		warnx("%s: before inflight: %d, after inflight: %d",
		    test, before_inflight, after_inflight);

	after_openfiles = getopenfiles();
	if (after_openfiles != before_openfiles)
		warnx("%s: before: %d, after: %d", test, before_openfiles,
		    after_openfiles);
}

static void
twosome_nothing(void)
{
	int inflight, openfiles;
	int sv[2];

	/*
	 * Create a pair, close in one order.
	 */
	test = "twosome_nothing1";
	printf("%s\n", test);
	save_sysctls(&inflight, &openfiles);
	my_socketpair(sv);
	close2(sv[0], sv[1]);
	test_sysctls(inflight, openfiles);

	/*
	 * Create a pair, close in the other order.
	 */
	test = "twosome_nothing2";
	printf("%s\n", test);
	save_sysctls(&inflight, &openfiles);
	my_socketpair(sv);
	close2(sv[0], sv[1]);
	test_sysctls(inflight, openfiles);
}

/*
 * Using a socket pair, send various endpoints over the pair and close in
 * various orders.
 */
static void
twosome_drop_work(const char *testname, int sendvia, int tosend, int closefirst)
{
	int inflight, openfiles;
	int sv[2];

	printf("%s\n", testname);
	test = testname;
	save_sysctls(&inflight, &openfiles);
	my_socketpair(sv);
	sendfd(sv[sendvia], sv[tosend]);
	if (closefirst == 0)
		close2(sv[0], sv[1]);
	else
		close2(sv[1], sv[0]);
	test_sysctls(inflight, openfiles);
}

static void
twosome_drop(void)
{

	/*
	 * In various combations, some wastefully symmetric, create socket
	 * pairs and send one or another endpoint over one or another
	 * endpoint, closing the endpoints in various orders.
	 */
	twosome_drop_work("twosome_drop1", 0, 0, 0);
	twosome_drop_work("twosome_drop2", 0, 0, 1);
	twosome_drop_work("twosome_drop3", 0, 1, 0);
	twosome_drop_work("twosome_drop4", 0, 1, 1);
	twosome_drop_work("twosome_drop5", 1, 0, 0);
	twosome_drop_work("twosome_drop6", 1, 0, 1);
	twosome_drop_work("twosome_drop7", 1, 1, 0);
	twosome_drop_work("twosome_drop8", 1, 1, 1);
}

static void
threesome_nothing(void)
{
	int inflight, openfiles;
	int s, sv[2];

	test = "threesome_nothing";
	printf("%s\n", test);
	save_sysctls(&inflight, &openfiles);
	alloc3fds(&s, sv);
	close3(s, sv[0], sv[1]);
	test_sysctls(inflight, openfiles);
}

/*
 * threesome_drop: create a pair and a spare, send the spare over the pair, and
 * close in various orders and make sure all the fds went away.
 */
static void
threesome_drop(void)
{
	int inflight, openfiles;
	int s, sv[2];

	/*
	 * threesome_drop1: close sent send receive
	 */
	test = "threesome_drop1";
	printf("%s\n", test);
	save_sysctls(&inflight, &openfiles);
	alloc3fds(&s, sv);
	sendfd(sv[0], s);
	close3(s, sv[0], sv[1]);
	test_sysctls(inflight, openfiles);

	/*
	 * threesome_drop2: close sent receive send
	 */
	test = "threesome_drop2";
	printf("%s\n", test);
	save_sysctls(&inflight, &openfiles);
	alloc3fds(&s, sv);
	sendfd(sv[0], s);
	close3(s, sv[1], sv[0]);
	test_sysctls(inflight, openfiles);

	/*
	 * threesome_drop3: close receive sent send
	 */
	test = "threesome_drop3";
	printf("%s\n", test);
	save_sysctls(&inflight, &openfiles);
	alloc3fds(&s, sv);
	sendfd(sv[0], s);
	close3(sv[1], s, sv[0]);
	test_sysctls(inflight, openfiles);

	/*
	 * threesome_drop4: close receive send sent
	 */
	test = "threesome_drop4";
	printf("%s\n", test);
	save_sysctls(&inflight, &openfiles);
	alloc3fds(&s, sv);
	sendfd(sv[0], s);
	close3(sv[1], sv[0], s);
	test_sysctls(inflight, openfiles);

	/*
	 * threesome_drop5: close send receive sent
	 */
	test = "threesome_drop5";
	printf("%s\n", test);
	save_sysctls(&inflight, &openfiles);
	alloc3fds(&s, sv);
	sendfd(sv[0], s);
	close3(sv[0], sv[1], s);
	test_sysctls(inflight, openfiles);

	/*
	 * threesome_drop6: close send sent receive
	 */
	test = "threesome_drop6";
	printf("%s\n", test);
	save_sysctls(&inflight, &openfiles);
	alloc3fds(&s, sv);
	close3(sv[0], s, sv[1]);
	test_sysctls(inflight, openfiles);
}

/*
 * Fivesome tests: create two socket pairs and a spare, send the spare over
 * the first socket pair, then send the first socket pair over the second
 * socket pair, and GC.  Do various closes at various points to exercise
 * various cases.
 */
static void
fivesome_nothing(void)
{
	int inflight, openfiles;
	int spare, sva[2], svb[2];

	test = "fivesome_nothing";
	printf("%s\n", test);
	save_sysctls(&inflight, &openfiles);
	alloc5fds(&spare, sva, svb);
	close5(spare, sva[0], sva[1], svb[0], svb[1]);
	test_sysctls(inflight, openfiles);
}

static void
fivesome_drop_work(const char *testname, int close_spare_after_send,
    int close_sva_after_send)
{
	int inflight, openfiles;
	int spare, sva[2], svb[2];

	printf("%s\n", testname);
	test = testname;
	save_sysctls(&inflight, &openfiles);
	alloc5fds(&spare, sva, svb);

	/*
	 * Send spare over sva.
	 */
	sendfd(sva[0], spare);
	if (close_spare_after_send)
		close(spare);

	/*
	 * Send sva over svb.
	 */
	sendfd(svb[0], sva[0]);
	sendfd(svb[0], sva[1]);
	if (close_sva_after_send)
		close2(sva[0], sva[1]);

	close2(svb[0], svb[1]);

	if (!close_sva_after_send)
		close2(sva[0], sva[1]);
	if (!close_spare_after_send)
		close(spare);

	test_sysctls(inflight, openfiles);
}

static void
fivesome_drop(void)
{

	fivesome_drop_work("fivesome_drop1", 0, 0);
	fivesome_drop_work("fivesome_drop2", 0, 1);
	fivesome_drop_work("fivesome_drop3", 1, 0);
	fivesome_drop_work("fivesome_drop4", 1, 1);
}

/*
 * Create a somewhat nasty dual-socket socket intended to upset the garbage
 * collector if mark-and-sweep is wrong.
 */
static void
complex_cycles(void)
{
	int inflight, openfiles;
	int spare, sva[2], svb[2];

	test = "complex_cycles";
	printf("%s\n", test);
	save_sysctls(&inflight, &openfiles);
	alloc5fds(&spare, sva, svb);
	sendfd(sva[0], svb[0]);
	sendfd(sva[0], svb[1]);
	sendfd(svb[0], sva[0]);
	sendfd(svb[0], sva[1]);
	sendfd(svb[0], spare);
	sendfd(sva[0], spare);
	close5(spare, sva[0], sva[1], svb[0], svb[1]);
	test_sysctls(inflight, openfiles);
}

/*
 * Listen sockets can also be passed over UNIX domain sockets, so test
 * various cases, including ones where listen sockets have waiting sockets
 * hanging off them...
 */
static void
listen_nothing(void)
{
	struct sockaddr_un sun;
	struct sockaddr_in sin;
	int inflight, openfiles;
	int s;

	test = "listen_nothing_unp";
	printf("%s\n", test);
	bzero(&sun, sizeof(sun));
	sun.sun_family = AF_LOCAL;
	sun.sun_len = sizeof(sun);
	snprintf(sun.sun_path, sizeof(sun.sun_path), "%s/%s", dpath, test);
	save_sysctls(&inflight, &openfiles);
	s = my_socket(PF_LOCAL, SOCK_STREAM, 0);
	my_bind(s, (struct sockaddr *)&sun, sizeof(sun));
	my_listen(s, -1);
	close(s);
	(void)unlink(sun.sun_path);
	test_sysctls(inflight, openfiles);

	test = "listen_nothing_inet";
	printf("%s\n", test);
	bzero(&sin, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_len = sizeof(sin);
	sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	sin.sin_port = htons(0);
	save_sysctls(&inflight, &openfiles);
	s = my_socket(PF_INET, SOCK_STREAM, 0);
	my_bind(s, (struct sockaddr *)&sin, sizeof(sin));
	my_listen(s, -1);
	close(s);
	test_sysctls(inflight, openfiles);
}

/*
 * Send a listen UDP socket over a UNIX domain socket.
 *
 * Send a listen TCP socket over a UNIX domain socket.
 *
 * Do each twice, with closing of the listen socket vs. socketpair in
 * different orders.
 */
static void
listen_drop(void)
{
	struct sockaddr_un sun;
	struct sockaddr_in sin;
	int inflight, openfiles;
	int s, sv[2];

	bzero(&sun, sizeof(sun));
	sun.sun_family = AF_LOCAL;
	sun.sun_len = sizeof(sun);

	/*
	 * Close listen socket first.
	 */
	test = "listen_drop_unp1";
	printf("%s\n", test);
	snprintf(sun.sun_path, sizeof(sun.sun_path), "%s/%s", dpath, test);
	save_sysctls(&inflight, &openfiles);
	s = my_socket(PF_LOCAL, SOCK_STREAM, 0);
	my_bind(s, (struct sockaddr *)&sun, sizeof(sun));
	my_listen(s, -1);
	my_socketpair(sv);
	sendfd(sv[0], s);
	close3(s, sv[0], sv[1]);
	test_sysctls(inflight, openfiles);

	/*
	 * Close socketpair first.
	 */
	test = "listen_drop_unp2";
	printf("%s\n", test);
	snprintf(sun.sun_path, sizeof(sun.sun_path), "%s/%s", dpath, test);
	save_sysctls(&inflight, &openfiles);
	s = my_socket(PF_LOCAL, SOCK_STREAM, 0);
	my_bind(s, (struct sockaddr *)&sun, sizeof(sun));
	my_listen(s, -1);
	my_socketpair(sv);
	sendfd(sv[0], s);
	close3(sv[0], sv[1], s);
	test_sysctls(inflight, openfiles);

	sin.sin_family = AF_INET;
	sin.sin_len = sizeof(sin);
	sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	sin.sin_port = htons(0);

	/*
	 * Close listen socket first.
	 */
	test = "listen_drop_inet1";
	printf("%s\n", test);
	bzero(&sun, sizeof(sun));
	save_sysctls(&inflight, &openfiles);
	s = my_socket(PF_INET, SOCK_STREAM, 0);
	my_bind(s, (struct sockaddr *)&sin, sizeof(sin));
	my_listen(s, -1);
	my_socketpair(sv);
	sendfd(sv[0], s);
	close3(s, sv[0], sv[1]);
	test_sysctls(inflight, openfiles);

	/*
	 * Close socketpair first.
	 */
	test = "listen_drop_inet2";
	printf("%s\n", test);
	bzero(&sun, sizeof(sun));
	save_sysctls(&inflight, &openfiles);
	s = my_socket(PF_INET, SOCK_STREAM, 0);
	my_bind(s, (struct sockaddr *)&sin, sizeof(sin));
	my_listen(s, -1);
	my_socketpair(sv);
	sendfd(sv[0], s);
	close3(sv[0], sv[1], s);
	test_sysctls(inflight, openfiles);
}

/*
 * Up things a notch with listen sockets: add connections that can be
 * accepted to the listen queues.
 */
static void
listen_connect_nothing(void)
{
	struct sockaddr_in sin;
	int slisten, sconnect, sv[2];
	int inflight, openfiles;
	socklen_t len;

	test = "listen_connect_nothing";
	printf("%s\n", test);
	save_sysctls(&inflight, &openfiles);

	slisten = my_socket(PF_INET, SOCK_STREAM, 0);
	my_bind(slisten, (struct sockaddr *)&sin, sizeof(sin));
	my_listen(slisten, -1);

	my_socketpair(sv);

	len = sizeof(sin);
	my_getsockname(slisten, (struct sockaddr *)&sin, &len);

	sconnect = my_socket(PF_INET, SOCK_STREAM, 0);
	setnonblock(sconnect);
	my_connect(sconnect, (struct sockaddr *)&sin, len);

	sleep(1);

	close4(slisten, sconnect, sv[0], sv[1]);

	test_sysctls(inflight, openfiles);
}

static void
listen_connect_drop(void)
{
	struct sockaddr_in sin;
	int slisten, sconnect, sv[2];
	int inflight, openfiles;
	socklen_t len;

	test = "listen_connect_drop";
	printf("%s\n", test);
	save_sysctls(&inflight, &openfiles);

	slisten = my_socket(PF_INET, SOCK_STREAM, 0);
	my_bind(slisten, (struct sockaddr *)&sin, sizeof(sin));
	my_listen(slisten, -1);

	my_socketpair(sv);

	len = sizeof(sin);
	my_getsockname(slisten, (struct sockaddr *)&sin, &len);

	sconnect = my_socket(PF_INET, SOCK_STREAM, 0);
	setnonblock(sconnect);
	my_connect(sconnect, (struct sockaddr *)&sin, len);

	sleep(1);
	sendfd(sv[0], slisten);
	close3(slisten, sv[0], sv[1]);
	sleep(1);
	close(sconnect);

	test_sysctls(inflight, openfiles);
}

static void
recursion(void)
{
	int fd[2], ff[2];
	int inflight, openfiles, deferred, deferred1;

	test = "recursion";
	printf("%s\n", test);
	save_sysctls(&inflight, &openfiles);
	deferred = getdeferred();

	my_socketpair(fd);

	for (;;) {
		if (socketpair(PF_UNIX, SOCK_STREAM, 0, ff) == -1) {
			if (errno == EMFILE || errno == ENFILE)
				break;
			err(-1, "socketpair");
		}
		sendfd(ff[0], fd[0]);
		sendfd(ff[0], fd[1]);
		close2(fd[1], fd[0]);
		fd[0] = ff[0];
		fd[1] = ff[1];
	}
	close2(fd[0], fd[1]);
	sleep(1);
	test_sysctls(inflight, openfiles);
	deferred1 = getdeferred();
	if (deferred != deferred1)
		errx(-1, "recursion: deferred before %d after %d", deferred,
		    deferred1);
}

#define	RMDIR	"rm -Rf "
int
main(void)
{
	char cmd[sizeof(RMDIR) + PATH_MAX];
	int serrno;
	pid_t pid;

	strlcpy(dpath, "/tmp/unpgc.XXXXXXXX", sizeof(dpath));
	if (mkdtemp(dpath) == NULL)
		err(-1, "mkdtemp");

	/*
	 * Set up a parent process to GC temporary storage when we're done.
	 */
	pid = fork();
	if (pid < 0) {
		serrno = errno;
		(void)rmdir(dpath);
		errno = serrno;
		err(-1, "fork");
	}
	if (pid > 0) {
		signal(SIGINT, SIG_IGN);
		while (waitpid(pid, NULL, 0) != pid);
		snprintf(cmd, sizeof(cmd), "%s %s", RMDIR, dpath);
		(void)system(cmd);
		exit(0);
	}

	printf("Start: inflight %d open %d\n", getinflight(),
	    getopenfiles());

	twosome_nothing();
	twosome_drop();

	threesome_nothing();
	threesome_drop();

	fivesome_nothing();
	fivesome_drop();

	complex_cycles();

	listen_nothing();
	listen_drop();

	listen_connect_nothing();
	listen_connect_drop();

	recursion();

	printf("Finish: inflight %d open %d\n", getinflight(),
	    getopenfiles());
	return (0);
}
