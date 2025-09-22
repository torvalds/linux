/* $OpenBSD: socket3.c,v 1.4 2005/10/30 23:59:43 fgsch Exp $ */
/* PUBLIC DOMAIN Oct 2002 <marc@snafu.org> */

/* Test blocking/non-blocking mode inheritance on accept */

#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>

#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>

#include "test.h"

/*
 * connect to the test port passed in arg, then close the connection
 * and return.
 */
static void *
sock_connect(void *arg)
{
	struct sockaddr_in sin;
	int port;
	int sock;

	SET_NAME("connect");
	port = *(int *)arg;
	CHECKe(sock = socket(AF_INET, SOCK_STREAM, 0));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	CHECKe(connect(sock, (struct sockaddr *)&sin, sizeof sin));
	CHECKe(close(sock));
	return NULL;
}

/*
 * listen for a connection, accept it using a non-blocking socket, and
 * verify that the blocking mode of the socket returned from accept is
 * also non-blocking
 */
static void *
sock_accept(void *arg)
{
	pthread_t connect_thread;
	struct pollfd fds;
	struct sockaddr_in sa;
	struct sockaddr accept_sa;
	int accept_fd;
	int accept_sa_size;
	int flags;
	int listen_fd;
	int port;

	SET_NAME("accept");

	/* listen for a connection */

	port = 6543;
	memset(&sa, 0, sizeof sa);
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	sa.sin_port = htons(port);
	CHECKe(listen_fd = socket(AF_INET, SOCK_STREAM, 0));
	printf("listen_fd = %d\n", listen_fd);
	while (1) {
		if (bind(listen_fd, (struct sockaddr *)&sa, sizeof(sa)) == 0)
			break;
		if (errno == EADDRINUSE) {
			sa.sin_port = htons(++port);
			continue;
		}
		DIE(errno, "bind");
	}
	CHECKe(listen(listen_fd, 2));

	/* Create another thread to connect to the listening socket. */
	CHECKr(pthread_create(&connect_thread, NULL, sock_connect,
	    (void *)&port));

	/*
	 * Use poll to check for a pending connection as the socket
	 * passed to accept will be in non-blocking mode.
	 */
	fds.fd = listen_fd;
	fds.events = POLLIN;
	CHECKe(poll(&fds, 1, INFTIM));

	/*
	 * set non blocking mode on the listening socket and close stdin
	 * (fd 0) so the accept will use fd 0 (needed to test boundary
	 * condition in the pthread accept code).
	 */
	flags = fcntl(listen_fd, F_GETFL);
        CHECKr(fcntl(listen_fd, F_SETFL, flags |= O_NONBLOCK));
	CHECKe(close(STDIN_FILENO));
	accept_sa_size = sizeof accept_sa;
	CHECKe(accept_fd = accept(listen_fd, &accept_sa, &accept_sa_size));
	/* verify O_NONBLOCK on the accepted fd */
	flags = fcntl(accept_fd, F_GETFL);
	printf("accept_fd = %d, flags = %x\n", accept_fd, flags);
	ASSERT(flags & O_NONBLOCK);
	CHECKe(close(listen_fd));
	CHECKe(close(accept_fd));
	CHECKr(pthread_join(connect_thread, NULL));
	return NULL;
}

int
main(int argc, char * argv[])
{
	pthread_t accept_thread;

	CHECKr(pthread_create(&accept_thread, NULL, sock_accept, NULL));
	CHECKr(pthread_join(accept_thread, NULL));
	SUCCEED;
}
