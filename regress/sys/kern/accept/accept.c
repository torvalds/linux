/*	$OpenBSD: accept.c,v 1.6 2021/12/13 16:56:49 deraadt Exp $	*/
/*
 *	Written by Artur Grabowski <art@openbsd.org>, 2002 Public Domain.
 */
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/un.h>

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <err.h>
#include <signal.h>
#include <string.h>

#define	SOCK_NAME	"test-sock"

int	child(void);

int
main(int argc, char *argv[])
{
	int listensock, sock;
	struct sockaddr_un sun, csun;
	int csunlen;
	int fd, lastfd;
	int status;
	int ischild = 0;

	/*
	 * Create the listen socket.
	 */
	if ((listensock = socket(PF_LOCAL, SOCK_STREAM, 0)) == -1)
		err(1, "socket");

	unlink(SOCK_NAME);
	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_LOCAL;
	strlcpy(sun.sun_path, SOCK_NAME, sizeof sun.sun_path);


	if (bind(listensock, (struct sockaddr *)&sun, sizeof(sun)) == -1)
		err(1, "bind");

	if (listen(listensock, 1) == -1)
		err(1, "listen");

	switch (fork()) {
	case -1:
		err(1, "fork");
	case 0:
		return child();
	}

	while ((fd = open("/dev/null", O_RDONLY)) >= 0)
		lastfd = fd;

	switch (fork()) {
	case -1:
		err(1, "fork");
	case 0:
		ischild = 1;
		close(lastfd);	/* Close one fd so that accept can succeed */
		sleep(2); /* sleep a bit so that we're the second to accept */
	}
	
	sock = accept(listensock, (struct sockaddr *)&csun, &csunlen);

	if (!ischild && sock >= 0)
		errx(1, "accept succeeded in parent");
	if (ischild && sock < 0)
		err(1, "accept failed in child");

	while (!ischild && wait4(-1, &status, 0, NULL) > 0)
		;

	return (0);
}

int
child()
{
	int i, fd, sock;
	struct sockaddr_un sun;

	/*
	 * Create socket and connect to the receiver.
	 */
	if ((sock = socket(PF_LOCAL, SOCK_STREAM, 0)) == -1)
		errx(1, "child socket");

	(void) memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_LOCAL;
	(void) strlcpy(sun.sun_path, SOCK_NAME, sizeof sun.sun_path);

	if (connect(sock, (struct sockaddr *)&sun, sizeof(sun)) == -1)
		err(1, "child connect");

	return (0);
}
