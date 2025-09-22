#include <sys/queue.h>
#include <sys/socket.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <imsg.h>

#include "extern.h"

static struct msgbuf	*httpq;

int filemode;

void
logx(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vwarnx(fmt, ap);
	va_end(ap);
}

time_t
getmonotime(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
		err(1, "clock_gettime");
	return (ts.tv_sec);
}

int
valid_origin(const char *uri, const char *proto)
{
	const char *to;

	/* extract end of host from proto URI */
	to = strstr(proto, "://");
	if (to == NULL)
		return 0;
	to += strlen("://");
	if ((to = strchr(to, '/')) == NULL)
		return 0;

	/* compare hosts including the / for the start of the path section */
	if (strncasecmp(uri, proto, to - proto + 1) != 0)
		return 0;

	return 1;
}

static void
http_request(unsigned int id, const char *uri, const char *last_mod, int fd)
{
	struct ibuf     *b;

	b = io_new_buffer();
	io_simple_buffer(b, &id, sizeof(id));
	io_str_buffer(b, uri);
	io_opt_str_buffer(b, last_mod);
	/* pass file as fd */
	b->fd = fd;
	io_close_buffer(httpq, b);
}

static const char *
http_result(enum http_result res)
{
	switch (res) {
	case HTTP_OK:
		return "OK";
	case HTTP_NOT_MOD:
		return "not modified";
	case HTTP_FAILED:
		return "failed";
	default:
		errx(1, "unknown http result: %d", res);
	}
}

static int
http_response(int fd)
{
	struct ibuf *b;
	unsigned int id;
	enum http_result res;
	char *lastmod;

	while (1) {
		switch (msgbuf_read(fd, httpq)) {
		case -1:
			err(1, "msgbuf_read");
		case 0:
			errx(1, "msgbuf_read: connection closed");
		}
		if ((b = io_buf_get(httpq)) != NULL)
			break;
	}

	io_read_buf(b, &id, sizeof(id));
	io_read_buf(b, &res, sizeof(res));
	io_read_opt_str(b, &lastmod);
	ibuf_free(b);

	printf("transfer %s", http_result(res));
	if (lastmod)
		printf(", last-modified: %s" , lastmod);
	printf("\n");
	return res == HTTP_FAILED;
}

int
main(int argc, char **argv)
{
	pid_t httppid;
	int error, fd[2], outfd, httpfd;
	int fl = SOCK_STREAM | SOCK_CLOEXEC;
	char *uri, *file, *mod;
	unsigned int req = 0;

	if (argc != 3 && argc != 4) {
		fprintf(stderr, "usage: test-http uri file [last-modified]\n");
		return 1;
	}
	uri = argv[1];
	file = argv[2];
	mod = argv[3];

	if (socketpair(AF_UNIX, fl, 0, fd) == -1)
		err(1, "socketpair");

	if ((httppid = fork()) == -1)
		err(1, "fork");

	if (httppid == 0) {
		close(fd[1]);

		if (pledge("stdio rpath inet dns recvfd", NULL) == -1)
			err(1, "pledge");

		proc_http(NULL, fd[0]);
		errx(1, "http process returned");
	}

	close(fd[0]);
	httpfd = fd[1];
	if ((httpq = msgbuf_new_reader(sizeof(size_t), io_parse_hdr, NULL)) ==
	    NULL)
		err(1, NULL);

	if ((outfd = open(file, O_WRONLY|O_CREAT|O_TRUNC, 0666)) == -1)
		err(1, "open %s", file);

	http_request(req++, uri, mod, outfd);
	if (msgbuf_write(httpfd, httpq) == -1)
		err(1, "write");
	error = http_response(httpfd);
	return error;
}
