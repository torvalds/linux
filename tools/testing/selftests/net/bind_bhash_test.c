// SPDX-License-Identifier: GPL-2.0
/*
 * This times how long it takes to bind to a port when the port already
 * has multiple sockets in its bhash table.
 *
 * In the setup(), we populate the port's bhash table with
 * MAX_THREADS * MAX_CONNECTIONS number of entries.
 */

#include <unistd.h>
#include <stdio.h>
#include <netdb.h>
#include <pthread.h>

#define MAX_THREADS 600
#define MAX_CONNECTIONS 40

static const char *bind_addr = "::1";
static const char *port;

static int fd_array[MAX_THREADS][MAX_CONNECTIONS];

static int bind_socket(int opt, const char *addr)
{
	struct addrinfo *res, hint = {};
	int sock_fd, reuse = 1, err;

	sock_fd = socket(AF_INET6, SOCK_STREAM, 0);
	if (sock_fd < 0) {
		perror("socket fd err");
		return -1;
	}

	hint.ai_family = AF_INET6;
	hint.ai_socktype = SOCK_STREAM;

	err = getaddrinfo(addr, port, &hint, &res);
	if (err) {
		perror("getaddrinfo failed");
		return -1;
	}

	if (opt) {
		err = setsockopt(sock_fd, SOL_SOCKET, opt, &reuse, sizeof(reuse));
		if (err) {
			perror("setsockopt failed");
			return -1;
		}
	}

	err = bind(sock_fd, res->ai_addr, res->ai_addrlen);
	if (err) {
		perror("failed to bind to port");
		return -1;
	}

	return sock_fd;
}

static void *setup(void *arg)
{
	int sock_fd, i;
	int *array = (int *)arg;

	for (i = 0; i < MAX_CONNECTIONS; i++) {
		sock_fd = bind_socket(SO_REUSEADDR | SO_REUSEPORT, bind_addr);
		if (sock_fd < 0)
			return NULL;
		array[i] = sock_fd;
	}

	return NULL;
}

int main(int argc, const char *argv[])
{
	int listener_fd, sock_fd, i, j;
	pthread_t tid[MAX_THREADS];
	clock_t begin, end;

	if (argc != 2) {
		printf("Usage: listener <port>\n");
		return -1;
	}

	port = argv[1];

	listener_fd = bind_socket(SO_REUSEADDR | SO_REUSEPORT, bind_addr);
	if (listen(listener_fd, 100) < 0) {
		perror("listen failed");
		return -1;
	}

	/* Set up threads to populate the bhash table entry for the port */
	for (i = 0; i < MAX_THREADS; i++)
		pthread_create(&tid[i], NULL, setup, fd_array[i]);

	for (i = 0; i < MAX_THREADS; i++)
		pthread_join(tid[i], NULL);

	begin = clock();

	/* Bind to the same port on a different address */
	sock_fd  = bind_socket(0, "2001:0db8:0:f101::1");

	end = clock();

	printf("time spent = %f\n", (double)(end - begin) / CLOCKS_PER_SEC);

	/* clean up */
	close(sock_fd);
	close(listener_fd);
	for (i = 0; i < MAX_THREADS; i++) {
		for (j = 0; i < MAX_THREADS; i++)
			close(fd_array[i][j]);
	}

	return 0;
}
