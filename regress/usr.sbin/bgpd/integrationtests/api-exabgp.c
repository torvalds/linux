#include <err.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void
usage(void)
{
	fprintf(stderr, "usage: api-exabgp [ -t timeout ] fifo\n");
	exit(1);
}

static int
fifo_open(const char *name)
{
	int fd;

	fd = open(name, O_RDONLY | O_NONBLOCK);
	if (fd == -1)
		err(1, "open %s", name);
	return fd;
}

int
main(int argc, char **argv)
{
	struct pollfd	pfd[2];
	char buf[512];
	const char *errstr, *fifo;
	int fd, ch, timeout = 0;
	time_t end, now;
	ssize_t n;

	while ((ch = getopt(argc, argv, "t:")) != -1) {
		switch (ch) {
		case 't':
			timeout = strtonum(optarg, 0, 120, &errstr);
			if (errstr != NULL)
				errx(1, "timeout is %s: %s", errstr, optarg);
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argv[0] == NULL)
		usage();
	fifo = argv[0];

	pfd[0].fd = 0;
	pfd[0].events = POLLIN;
	pfd[1].fd = fd = fifo_open(fifo);
	pfd[1].events = POLLIN;

	end = time(NULL) + timeout;
	while (1) {
		now = time(NULL);
		if (timeout != 0 && end < now) {
			if (write(1, "shutdown\n", 9) != 9)
				errx(1, "bad write to stdout");
		}
		if (poll(pfd, 2, 1000) == -1)
			err(1, "poll");

		if (pfd[0].revents & POLLIN) {
			n = read(0, buf, sizeof(buf));
			if (n == -1)
				err(1, "read stdin");
			if (n > 2 && strncmp(buf + n - 2, "\n\n", 2) == 0)
				n--;
			if (write(2, buf, n) != n)
				errx(1, "bad write to stderr");
			if (n > 8 && strncmp(buf, "shutdown", 8) == 0)
				errx(0, "exabgp shutdown");
		}
		if (pfd[1].revents & POLLIN) {
			n = read(fd, buf, sizeof(buf));
			if (n == -1)
				err(1, "read fifo");
			if (n > 0) {
				if (write(1, buf, n) != n)
					errx(1, "bad write to stdout");
			}
		}
		if (pfd[1].revents & POLLHUP) {
			/* re-open fifo */
			close(fd);
			pfd[1].fd = fd = fifo_open(fifo);
		}
	}
}
