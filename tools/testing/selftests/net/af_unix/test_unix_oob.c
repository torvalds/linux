// SPDX-License-Identifier: GPL-2.0-or-later
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <sys/un.h>
#include <sys/signal.h>
#include <sys/poll.h>

static int pipefd[2];
static int signal_recvd;
static pid_t producer_id;
static char sock_name[32];

static void sig_hand(int sn, siginfo_t *si, void *p)
{
	signal_recvd = sn;
}

static int set_sig_handler(int signal)
{
	struct sigaction sa;

	sa.sa_sigaction = sig_hand;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_SIGINFO | SA_RESTART;

	return sigaction(signal, &sa, NULL);
}

static void set_filemode(int fd, int set)
{
	int flags = fcntl(fd, F_GETFL, 0);

	if (set)
		flags &= ~O_NONBLOCK;
	else
		flags |= O_NONBLOCK;
	fcntl(fd, F_SETFL, flags);
}

static void signal_producer(int fd)
{
	char cmd;

	cmd = 'S';
	write(fd, &cmd, sizeof(cmd));
}

static void wait_for_signal(int fd)
{
	char buf[5];

	read(fd, buf, 5);
}

static void die(int status)
{
	fflush(NULL);
	unlink(sock_name);
	kill(producer_id, SIGTERM);
	exit(status);
}

int is_sioctatmark(int fd)
{
	int ans = -1;

	if (ioctl(fd, SIOCATMARK, &ans, sizeof(ans)) < 0) {
#ifdef DEBUG
		perror("SIOCATMARK Failed");
#endif
	}
	return ans;
}

void read_oob(int fd, char *c)
{

	*c = ' ';
	if (recv(fd, c, sizeof(*c), MSG_OOB) < 0) {
#ifdef DEBUG
		perror("Reading MSG_OOB Failed");
#endif
	}
}

int read_data(int pfd, char *buf, int size)
{
	int len = 0;

	memset(buf, size, '0');
	len = read(pfd, buf, size);
#ifdef DEBUG
	if (len < 0)
		perror("read failed");
#endif
	return len;
}

static void wait_for_data(int pfd, int event)
{
	struct pollfd pfds[1];

	pfds[0].fd = pfd;
	pfds[0].events = event;
	poll(pfds, 1, -1);
}

void producer(struct sockaddr_un *consumer_addr)
{
	int cfd;
	char buf[64];
	int i;

	memset(buf, 'x', sizeof(buf));
	cfd = socket(AF_UNIX, SOCK_STREAM, 0);

	wait_for_signal(pipefd[0]);
	if (connect(cfd, (struct sockaddr *)consumer_addr,
		     sizeof(*consumer_addr)) != 0) {
		perror("Connect failed");
		kill(0, SIGTERM);
		exit(1);
	}

	for (i = 0; i < 2; i++) {
		/* Test 1: Test for SIGURG and OOB */
		wait_for_signal(pipefd[0]);
		memset(buf, 'x', sizeof(buf));
		buf[63] = '@';
		send(cfd, buf, sizeof(buf), MSG_OOB);

		wait_for_signal(pipefd[0]);

		/* Test 2: Test for OOB being overwitten */
		memset(buf, 'x', sizeof(buf));
		buf[63] = '%';
		send(cfd, buf, sizeof(buf), MSG_OOB);

		memset(buf, 'x', sizeof(buf));
		buf[63] = '#';
		send(cfd, buf, sizeof(buf), MSG_OOB);

		wait_for_signal(pipefd[0]);

		/* Test 3: Test for SIOCATMARK */
		memset(buf, 'x', sizeof(buf));
		buf[63] = '@';
		send(cfd, buf, sizeof(buf), MSG_OOB);

		memset(buf, 'x', sizeof(buf));
		buf[63] = '%';
		send(cfd, buf, sizeof(buf), MSG_OOB);

		memset(buf, 'x', sizeof(buf));
		send(cfd, buf, sizeof(buf), 0);

		wait_for_signal(pipefd[0]);

		/* Test 4: Test for 1byte OOB msg */
		memset(buf, 'x', sizeof(buf));
		buf[0] = '@';
		send(cfd, buf, 1, MSG_OOB);
	}
}

int
main(int argc, char **argv)
{
	int lfd, pfd;
	struct sockaddr_un consumer_addr, paddr;
	socklen_t len = sizeof(consumer_addr);
	char buf[1024];
	int on = 0;
	char oob;
	int atmark;

	lfd = socket(AF_UNIX, SOCK_STREAM, 0);
	memset(&consumer_addr, 0, sizeof(consumer_addr));
	consumer_addr.sun_family = AF_UNIX;
	sprintf(sock_name, "unix_oob_%d", getpid());
	unlink(sock_name);
	strcpy(consumer_addr.sun_path, sock_name);

	if ((bind(lfd, (struct sockaddr *)&consumer_addr,
		  sizeof(consumer_addr))) != 0) {
		perror("socket bind failed");
		exit(1);
	}

	pipe(pipefd);

	listen(lfd, 1);

	producer_id = fork();
	if (producer_id == 0) {
		producer(&consumer_addr);
		exit(0);
	}

	set_sig_handler(SIGURG);
	signal_producer(pipefd[1]);

	pfd = accept(lfd, (struct sockaddr *) &paddr, &len);
	fcntl(pfd, F_SETOWN, getpid());

	signal_recvd = 0;
	signal_producer(pipefd[1]);

	/* Test 1:
	 * veriyf that SIGURG is
	 * delivered, 63 bytes are
	 * read, oob is '@', and POLLPRI works.
	 */
	wait_for_data(pfd, POLLPRI);
	read_oob(pfd, &oob);
	len = read_data(pfd, buf, 1024);
	if (!signal_recvd || len != 63 || oob != '@') {
		fprintf(stderr, "Test 1 failed sigurg %d len %d %c\n",
			 signal_recvd, len, oob);
			die(1);
	}

	signal_recvd = 0;
	signal_producer(pipefd[1]);

	/* Test 2:
	 * Verify that the first OOB is over written by
	 * the 2nd one and the first OOB is returned as
	 * part of the read, and sigurg is received.
	 */
	wait_for_data(pfd, POLLIN | POLLPRI);
	len = 0;
	while (len < 70)
		len = recv(pfd, buf, 1024, MSG_PEEK);
	len = read_data(pfd, buf, 1024);
	read_oob(pfd, &oob);
	if (!signal_recvd || len != 127 || oob != '#') {
		fprintf(stderr, "Test 2 failed, sigurg %d len %d OOB %c\n",
		signal_recvd, len, oob);
		die(1);
	}

	signal_recvd = 0;
	signal_producer(pipefd[1]);

	/* Test 3:
	 * verify that 2nd oob over writes
	 * the first one and read breaks at
	 * oob boundary returning 127 bytes
	 * and sigurg is received and atmark
	 * is set.
	 * oob is '%' and second read returns
	 * 64 bytes.
	 */
	len = 0;
	wait_for_data(pfd, POLLIN | POLLPRI);
	while (len < 150)
		len = recv(pfd, buf, 1024, MSG_PEEK);
	len = read_data(pfd, buf, 1024);
	atmark = is_sioctatmark(pfd);
	read_oob(pfd, &oob);

	if (!signal_recvd || len != 127 || oob != '%' || atmark != 1) {
		fprintf(stderr,
			"Test 3 failed, sigurg %d len %d OOB %c atmark %d\n",
			signal_recvd, len, oob, atmark);
		die(1);
	}

	signal_recvd = 0;

	len = read_data(pfd, buf, 1024);
	if (len != 64) {
		fprintf(stderr, "Test 3.1 failed, sigurg %d len %d OOB %c\n",
			signal_recvd, len, oob);
		die(1);
	}

	signal_recvd = 0;
	signal_producer(pipefd[1]);

	/* Test 4:
	 * verify that a single byte
	 * oob message is delivered.
	 * set non blocking mode and
	 * check proper error is
	 * returned and sigurg is
	 * received and correct
	 * oob is read.
	 */

	set_filemode(pfd, 0);

	wait_for_data(pfd, POLLIN | POLLPRI);
	len = read_data(pfd, buf, 1024);
	if ((len == -1) && (errno == 11))
		len = 0;

	read_oob(pfd, &oob);

	if (!signal_recvd || len != 0 || oob != '@') {
		fprintf(stderr, "Test 4 failed, sigurg %d len %d OOB %c\n",
			 signal_recvd, len, oob);
		die(1);
	}

	set_filemode(pfd, 1);

	/* Inline Testing */

	on = 1;
	if (setsockopt(pfd, SOL_SOCKET, SO_OOBINLINE, &on, sizeof(on))) {
		perror("SO_OOBINLINE");
		die(1);
	}

	signal_recvd = 0;
	signal_producer(pipefd[1]);

	/* Test 1 -- Inline:
	 * Check that SIGURG is
	 * delivered and 63 bytes are
	 * read and oob is '@'
	 */

	wait_for_data(pfd, POLLIN | POLLPRI);
	len = read_data(pfd, buf, 1024);

	if (!signal_recvd || len != 63) {
		fprintf(stderr, "Test 1 Inline failed, sigurg %d len %d\n",
			signal_recvd, len);
		die(1);
	}

	len = read_data(pfd, buf, 1024);

	if (len != 1) {
		fprintf(stderr,
			 "Test 1.1 Inline failed, sigurg %d len %d oob %c\n",
			 signal_recvd, len, oob);
		die(1);
	}

	signal_recvd = 0;
	signal_producer(pipefd[1]);

	/* Test 2 -- Inline:
	 * Verify that the first OOB is over written by
	 * the 2nd one and read breaks correctly on
	 * 2nd OOB boundary with the first OOB returned as
	 * part of the read, and sigurg is delivered and
	 * siocatmark returns true.
	 * next read returns one byte, the oob byte
	 * and siocatmark returns false.
	 */
	len = 0;
	wait_for_data(pfd, POLLIN | POLLPRI);
	while (len < 70)
		len = recv(pfd, buf, 1024, MSG_PEEK);
	len = read_data(pfd, buf, 1024);
	atmark = is_sioctatmark(pfd);
	if (len != 127 || atmark != 1 || !signal_recvd) {
		fprintf(stderr, "Test 2 Inline failed, len %d atmark %d\n",
			 len, atmark);
		die(1);
	}

	len = read_data(pfd, buf, 1024);
	atmark = is_sioctatmark(pfd);
	if (len != 1 || buf[0] != '#' || atmark == 1) {
		fprintf(stderr, "Test 2.1 Inline failed, len %d data %c atmark %d\n",
			len, buf[0], atmark);
		die(1);
	}

	signal_recvd = 0;
	signal_producer(pipefd[1]);

	/* Test 3 -- Inline:
	 * verify that 2nd oob over writes
	 * the first one and read breaks at
	 * oob boundary returning 127 bytes
	 * and sigurg is received and siocatmark
	 * is true after the read.
	 * subsequent read returns 65 bytes
	 * because of oob which should be '%'.
	 */
	len = 0;
	wait_for_data(pfd, POLLIN | POLLPRI);
	while (len < 126)
		len = recv(pfd, buf, 1024, MSG_PEEK);
	len = read_data(pfd, buf, 1024);
	atmark = is_sioctatmark(pfd);
	if (!signal_recvd || len != 127 || !atmark) {
		fprintf(stderr,
			 "Test 3 Inline failed, sigurg %d len %d data %c\n",
			 signal_recvd, len, buf[0]);
		die(1);
	}

	len = read_data(pfd, buf, 1024);
	atmark = is_sioctatmark(pfd);
	if (len != 65 || buf[0] != '%' || atmark != 0) {
		fprintf(stderr,
			 "Test 3.1 Inline failed, len %d oob %c atmark %d\n",
			 len, buf[0], atmark);
		die(1);
	}

	signal_recvd = 0;
	signal_producer(pipefd[1]);

	/* Test 4 -- Inline:
	 * verify that a single
	 * byte oob message is delivered
	 * and read returns one byte, the oob
	 * byte and sigurg is received
	 */
	wait_for_data(pfd, POLLIN | POLLPRI);
	len = read_data(pfd, buf, 1024);
	if (!signal_recvd || len != 1 || buf[0] != '@') {
		fprintf(stderr,
			"Test 4 Inline failed, signal %d len %d data %c\n",
		signal_recvd, len, buf[0]);
		die(1);
	}
	die(0);
}
