// SPDX-License-Identifier: GPL-2.0

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT 12345
#define RUNTIME 10

static struct {
	unsigned int timeout;
	unsigned int port;
} opts = {
	.timeout = RUNTIME,
	.port = PORT,
};

static void handler(int sig)
{
	_exit(sig == SIGALRM ? 0 : 1);
}

static void set_timeout(void)
{
	struct sigaction action = {
		.sa_handler = handler,
	};

	sigaction(SIGALRM, &action, NULL);

	alarm(opts.timeout);
}

static void do_connect(const struct sockaddr_in *dst)
{
	int s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	if (s >= 0)
		fcntl(s, F_SETFL, O_NONBLOCK);

	connect(s, (struct sockaddr *)dst, sizeof(*dst));
	close(s);
}

static void do_accept(const struct sockaddr_in *src)
{
	int c, one = 1, s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	if (s < 0)
		return;

	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));

	bind(s, (struct sockaddr *)src, sizeof(*src));

	listen(s, 16);

	c = accept(s, NULL, NULL);
	if (c >= 0)
		close(c);

	close(s);
}

static int accept_loop(void)
{
	struct sockaddr_in src = {
		.sin_family = AF_INET,
		.sin_port = htons(opts.port),
	};

	inet_pton(AF_INET, "127.0.0.1", &src.sin_addr);

	set_timeout();

	for (;;)
		do_accept(&src);

	return 1;
}

static int connect_loop(void)
{
	struct sockaddr_in dst = {
		.sin_family = AF_INET,
		.sin_port = htons(opts.port),
	};

	inet_pton(AF_INET, "127.0.0.1", &dst.sin_addr);

	set_timeout();

	for (;;)
		do_connect(&dst);

	return 1;
}

static void parse_opts(int argc, char **argv)
{
	int c;

	while ((c = getopt(argc, argv, "t:p:")) != -1) {
		switch (c) {
		case 't':
			opts.timeout = atoi(optarg);
			break;
		case 'p':
			opts.port = atoi(optarg);
			break;
		}
	}
}

int main(int argc, char *argv[])
{
	pid_t p;

	parse_opts(argc, argv);

	p = fork();
	if (p < 0)
		return 111;

	if (p > 0)
		return accept_loop();

	return connect_loop();
}
