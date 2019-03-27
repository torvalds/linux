/* $FreeBSD$ */

#include <sys/socket.h>
#include <sys/select.h>
#include <sys/stat.h>

#include <err.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define	FIFONAME	"fifo.tmp"
#define	FT_END		3
#define	FT_FIFO		2
#define	FT_PIPE		0
#define	FT_SOCKETPAIR	1

#define	SETUP(fd, rfds, tv) do {				\
	FD_ZERO(&(rfds));					\
	FD_SET((fd), &(rfds));					\
	(tv).tv_sec = 0;					\
	(tv).tv_usec = 0;					\
} while (0)

static int filetype;

static const char *
decode_events(int events)
{
	return (events ? "set" : "clear");
}

static void
report(int num, const char *state, int expected, int got)
{
	if (!expected == !got)
		printf("ok %-2d    ", num);
	else
		printf("not ok %-2d", num);
	printf(" %s state %s: expected %s; got %s\n",
	    filetype == FT_PIPE ? "Pipe" :
	    filetype == FT_SOCKETPAIR ? "Sock" : "FIFO",
	    state, decode_events(expected), decode_events(got));
	fflush(stdout);
}

static pid_t cpid;
static pid_t ppid;
static volatile sig_atomic_t state;

static void
catch(int sig)
{
	state++;
}

static void
child(int fd, int num)
{
	fd_set rfds;
	struct timeval tv;
	int fd1, fd2;
	char buf[256];

	if (filetype == FT_FIFO) {
		fd = open(FIFONAME, O_RDONLY | O_NONBLOCK);
		if (fd < 0)
			err(1, "open for read");
	}
	if (fd >= FD_SETSIZE)
		errx(1, "fd = %d too large for select()", fd);

	if (filetype == FT_FIFO) {
		SETUP(fd, rfds, tv);
		if (select(fd + 1, &rfds, NULL, NULL, &tv) < 0)
			err(1, "select");
		/*
		 * This state (a reader for which there has never been a
		 * writer) is reported quite differently for select() than
		 * for poll().  select() must see a ready-to-read descriptor
		 * since read() will see EOF and not block; it cannot
		 * distinguish this state from the one of a reader for which
		 * there has been a writer but all writers have gone away
		 * and all data has been read.  poll() and distinguish these
		 * states by returning POLLHUP only for the latter; it does
		 * this, although this makes it inconsistent with the
		 * blockability of read() in the former.
		 */
		report(num++, "0", 1, FD_ISSET(fd, &rfds));
	}
	kill(ppid, SIGUSR1);

	usleep(1);
	while (state != 1)
		;
	if (filetype != FT_FIFO) {
		/*
		 * The connection cannot be reestablished.  Use the code that
		 * delays the read until after the writer disconnects since
		 * that case is more interesting.
		 */
		state = 4;
		goto state4;
	}
	SETUP(fd, rfds, tv);
	if (select(fd + 1, &rfds, NULL, NULL, &tv) < 0)
		err(1, "select");
	report(num++, "1", 0, FD_ISSET(fd, &rfds));
	kill(ppid, SIGUSR1);

	usleep(1);
	while (state != 2)
		;
	SETUP(fd, rfds, tv);
	if (select(fd + 1, &rfds, NULL, NULL, &tv) < 0)
		err(1, "select");
	report(num++, "2", 1, FD_ISSET(fd, &rfds));
	if (read(fd, buf, sizeof buf) != 1)
		err(1, "read");
	SETUP(fd, rfds, tv);
	if (select(fd + 1, &rfds, NULL, NULL, &tv) < 0)
		err(1, "select");
	report(num++, "2a", 0, FD_ISSET(fd, &rfds));
	kill(ppid, SIGUSR1);

	usleep(1);
	while (state != 3)
		;
	SETUP(fd, rfds, tv);
	if (select(fd + 1, &rfds, NULL, NULL, &tv) < 0)
		err(1, "select");
	report(num++, "3", 1, FD_ISSET(fd, &rfds));
	kill(ppid, SIGUSR1);

	/*
	 * Now we expect a new writer, and a new connection too since
	 * we read all the data.  The only new point is that we didn't
	 * start quite from scratch since the read fd is not new.  Check
	 * startup state as above, but don't do the read as above.
	 */
	usleep(1);
	while (state != 4)
		;
state4:
	SETUP(fd, rfds, tv);
	if (select(fd + 1, &rfds, NULL, NULL, &tv) < 0)
		err(1, "select");
	report(num++, "4", 0, FD_ISSET(fd, &rfds));
	kill(ppid, SIGUSR1);

	usleep(1);
	while (state != 5)
		;
	SETUP(fd, rfds, tv);
	if (select(fd + 1, &rfds, NULL, NULL, &tv) < 0)
		err(1, "select");
	report(num++, "5", 1, FD_ISSET(fd, &rfds));
	kill(ppid, SIGUSR1);

	usleep(1);
	while (state != 6)
		;
	/*
	 * Now we have no writer, but should still have data from the old
	 * writer.  Check that we have a data-readable condition, and that
	 * the data can be read in the usual way.
	 */
	SETUP(fd, rfds, tv);
	if (select(fd + 1, &rfds, NULL, NULL, &tv) < 0)
		err(1, "select");
	report(num++, "6", 1, FD_ISSET(fd, &rfds));
	if (read(fd, buf, sizeof buf) != 1)
		err(1, "read");
	SETUP(fd, rfds, tv);
	if (select(fd + 1, &rfds, NULL, NULL, &tv) < 0)
		err(1, "select");
	report(num++, "6a", 1, FD_ISSET(fd, &rfds));
	if (filetype == FT_FIFO) {
		/*
		 * Check that the readable-data condition is sticky for a
		 * new reader and for the old reader.  We really only have
		 * a hangup condition, but select() can only see this as
		 * a readable-data condition for null data.  select()
		 * cannot distinguish this state from the initial state
		 * where there is a reader but has never been a writer, so
		 * the following tests (to follow the pattern in pipepoll.c)
		 * essentially test state 0 again.
		 */
		fd2 = open(FIFONAME, O_RDONLY | O_NONBLOCK);
		if (fd2 < 0)
			err(1, "open for read");
		fd1 = fd;
		fd = fd2;
		SETUP(fd, rfds, tv);
		if (select(fd + 1, &rfds, NULL, NULL, &tv) < 0)
			err(1, "select");
		report(num++, "6b", 1, FD_ISSET(fd, &rfds));
		fd = fd1;
		SETUP(fd, rfds, tv);
		if (select(fd + 1, &rfds, NULL, NULL, &tv) < 0)
			err(1, "select");
		report(num++, "6c", 1, FD_ISSET(fd, &rfds));
		close(fd2);
		SETUP(fd, rfds, tv);
		if (select(fd + 1, &rfds, NULL, NULL, &tv) < 0)
			err(1, "select");
		report(num++, "6d", 1, FD_ISSET(fd, &rfds));
	}
	close(fd);
	kill(ppid, SIGUSR1);

	exit(0);
}

static void
parent(int fd)
{
	usleep(1);
	while (state != 1)
		;
	if (filetype == FT_FIFO) {
		fd = open(FIFONAME, O_WRONLY | O_NONBLOCK);
		if (fd < 0)
			err(1, "open for write");
	}
	kill(cpid, SIGUSR1);

	usleep(1);
	while (state != 2)
		;
	if (write(fd, "", 1) != 1)
		err(1, "write");
	kill(cpid, SIGUSR1);

	usleep(1);
	while (state != 3)
		;
	if (close(fd) != 0)
		err(1, "close for write");
	kill(cpid, SIGUSR1);

	usleep(1);
	while (state != 4)
		;
	if (filetype != FT_FIFO)
		return;
	fd = open(FIFONAME, O_WRONLY | O_NONBLOCK);
	if (fd < 0)
		err(1, "open for write");
	kill(cpid, SIGUSR1);

	usleep(1);
	while (state != 5)
		;
	if (write(fd, "", 1) != 1)
		err(1, "write");
	kill(cpid, SIGUSR1);

	usleep(1);
	while (state != 6)
		;
	if (close(fd) != 0)
		err(1, "close for write");
	kill(cpid, SIGUSR1);

	usleep(1);
	while (state != 7)
		;
}

int
main(void)
{
	int fd[2], num;

	num = 1;
	printf("1..20\n");
	fflush(stdout);
	signal(SIGUSR1, catch);
	ppid = getpid();
	for (filetype = 0; filetype < FT_END; filetype++) {
		switch (filetype) {
		case FT_FIFO:
			if (mkfifo(FIFONAME, 0666) != 0)
				err(1, "mkfifo");
			fd[0] = -1;
			fd[1] = -1;
			break;
		case FT_SOCKETPAIR:
			if (socketpair(AF_UNIX, SOCK_STREAM, AF_UNSPEC,
			    fd) != 0)
				err(1, "socketpair");
			break;
		case FT_PIPE:
			if (pipe(fd) != 0)
				err(1, "pipe");
			break;
		}
		state = 0;
		switch (cpid = fork()) {
		case -1:
			err(1, "fork");
		case 0:
			(void)close(fd[1]);
			child(fd[0], num);
			break;
		default:
			(void)close(fd[0]);
			parent(fd[1]);
			break;
		}
		num += filetype == FT_FIFO ? 12 : 4;
	}
	(void)unlink(FIFONAME);
	return (0);
}
