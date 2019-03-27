/* $FreeBSD$ */

#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <err.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static const char *
decode_events(int events)
{
	char *ncresult;
	const char *result;

	switch (events) {
	case POLLIN:
		result = "POLLIN";
		break;
	case POLLOUT:
		result = "POLLOUT";
		break;
	case POLLIN | POLLOUT:
		result = "POLLIN | POLLOUT";
		break;
	case POLLHUP:
		result = "POLLHUP";
		break;
	case POLLIN | POLLHUP:
		result = "POLLIN | POLLHUP";
		break;
	case POLLOUT | POLLHUP:
		result = "POLLOUT | POLLHUP";
		break;
	case POLLIN | POLLOUT | POLLHUP:
		result = "POLLIN | POLLOUT | POLLHUP";
		break;
	default:
		asprintf(&ncresult, "%#x", events);
		result = ncresult;
		break;
	}
	return (result);
}

static void
report(int num, const char *state, int expected, int got)
{
	if (expected == got)
		printf("ok %-2d    ", num);
	else
		printf("not ok %-2d", num);
	printf(" state %s: expected %s; got %s\n",
	    state, decode_events(expected), decode_events(got));
	fflush(stdout);
}

static int
set_nonblocking(int sck)
{
	int flags;

	flags = fcntl(sck, F_GETFL, 0);
	flags |= O_NONBLOCK;

	if (fcntl(sck, F_SETFL, flags))
		return -1;

	return 0;
}

static char largeblock[1048576]; /* should be more than AF_UNIX sockbuf size */
static int fd[2];
static struct pollfd pfd0;
static struct pollfd pfd1;

void
setup(void)
{
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, fd) != 0)
		err(1, "socketpair");
	if (set_nonblocking(fd[0]) == -1)
		err(1, "fcntl");
	if (set_nonblocking(fd[1]) == -1)
		err(1, "fcntl");
	pfd0.fd = fd[0];
	pfd0.events = POLLIN | POLLOUT;
	pfd1.fd = fd[1];
	pfd1.events = POLLIN | POLLOUT;
}

int
main(void)
{
	int num;

	num = 1;
	printf("1..18\n");
	fflush(stdout);

	/* Large write with close */
	setup();
	if (poll(&pfd0, 1, 0) == -1)
		err(1, "poll");
	report(num++, "initial 0", POLLOUT, pfd0.revents);
	if (poll(&pfd1, 1, 0) == -1)
		err(1, "poll");
	report(num++, "initial 1", POLLOUT, pfd1.revents);
	if (write(fd[0], largeblock, sizeof(largeblock)) == -1)
		err(1, "write");
	if (poll(&pfd0, 1, 0) == -1)
		err(1, "poll");
	report(num++, "after large write", 0, pfd0.revents);
	if (poll(&pfd1, 1, 0) == -1)
		err(1, "poll");
	report(num++, "other side after large write", POLLIN | POLLOUT, pfd1.revents);
	close(fd[0]);
	if (poll(&pfd1, 1, 0) == -1)
		err(1, "poll");
	report(num++, "other side after close", POLLIN | POLLHUP, pfd1.revents);
	if (read(fd[1], largeblock, sizeof(largeblock)) == -1)
		err(1, "read");
	if (poll(&pfd1, 1, 0) == -1)
		err(1, "poll");
	report(num++, "other side after reading input", POLLHUP, pfd1.revents);
	close(fd[1]);

	/* With shutdown(SHUT_WR) */
	setup();
	if (shutdown(fd[0], SHUT_WR) == -1)
		err(1, "shutdown");
	if (poll(&pfd0, 1, 0) == -1)
		err(1, "poll");
	report(num++, "after shutdown(SHUT_WR)", POLLOUT, pfd0.revents);
	if (poll(&pfd1, 1, 0) == -1)
		err(1, "poll");
	report(num++, "other side after shutdown(SHUT_WR)", POLLIN | POLLOUT, pfd1.revents);
	switch (read(fd[1], largeblock, sizeof(largeblock))) {
		case 0:
			break;
		case -1:
			err(1, "read after other side shutdown");
			break;
		default:
			errx(1, "kernel made up data that was never written");
	}
	if (poll(&pfd1, 1, 0) == -1)
		err(1, "poll");
	report(num++, "other side after reading EOF", POLLIN | POLLOUT, pfd1.revents);
	if (write(fd[1], largeblock, sizeof(largeblock)) == -1)
		err(1, "write");
	if (poll(&pfd0, 1, 0) == -1)
		err(1, "poll");
	report(num++, "after data from other side", POLLIN | POLLOUT, pfd0.revents);
	if (poll(&pfd1, 1, 0) == -1)
		err(1, "poll");
	report(num++, "after writing", POLLIN, pfd1.revents);
	if (shutdown(fd[1], SHUT_WR) == -1)
		err(1, "shutdown second");
	if (poll(&pfd0, 1, 0) == -1)
		err(1, "poll");
	report(num++, "after second shutdown", POLLIN | POLLHUP, pfd0.revents);
	if (poll(&pfd1, 1, 0) == -1)
		err(1, "poll");
	report(num++, "after second shutdown", POLLHUP, pfd1.revents);
	close(fd[0]);
	if (poll(&pfd1, 1, 0) == -1)
		err(1, "poll");
	report(num++, "after close", POLLHUP, pfd1.revents);
	close(fd[1]);

	/*
	 * With shutdown(SHUT_RD)
	 * Note that shutdown(SHUT_WR) is passed to the peer, but
	 * shutdown(SHUT_RD) is not.
	 */
	setup();
	if (shutdown(fd[0], SHUT_RD) == -1)
		err(1, "shutdown");
	if (poll(&pfd0, 1, 0) == -1)
		err(1, "poll");
	report(num++, "after shutdown(SHUT_RD)", POLLIN | POLLOUT, pfd0.revents);
	if (poll(&pfd1, 1, 0) == -1)
		err(1, "poll");
	report(num++, "other side after shutdown(SHUT_RD)", POLLOUT, pfd1.revents);
	if (shutdown(fd[0], SHUT_WR) == -1)
		err(1, "shutdown");
	if (poll(&pfd0, 1, 0) == -1)
		err(1, "poll");
	report(num++, "after shutdown(SHUT_WR)", POLLHUP, pfd0.revents);
	if (poll(&pfd1, 1, 0) == -1)
		err(1, "poll");
	report(num++, "other side after shutdown(SHUT_WR)", POLLIN | POLLOUT, pfd1.revents);
	close(fd[0]);
	close(fd[1]);

	return (0);
}
