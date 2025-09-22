/* $OpenBSD: getpeereid_test.c,v 1.5 2024/08/23 12:56:26 anton Exp $ */
/* Written by Marc Espie in 2006 */
/* Public domain */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <err.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

char path[1024];
char *dir;

char *
check_id(int fd)
{
	uid_t sockuid, myuid;
	gid_t sockgid, mygid;
	static char problem[1024];

	if (getpeereid(fd, &sockuid, &sockgid) == -1) {
		snprintf(problem, sizeof problem, "getpeereid: %s", 
		    strerror(errno));
		return problem;
	}
	myuid = geteuid();
	mygid = getgid();
	if (myuid != sockuid) {
		snprintf(problem, sizeof problem, "uid discrepancy %ld vs %ld",
			(long)myuid, (long)sockuid);
		return problem;
	}
	if (mygid != sockgid) {
		snprintf(problem, sizeof problem, "gid discrepancy %ld vs %ld",
			(long)mygid, (long)sockgid);
		return problem;
	}
	return NULL;
}

void
client(struct sockaddr_un *sun)
{
	int s;
	int i;
	int r;
	char *problem;

	s = socket(AF_UNIX, SOCK_STREAM, 0);
	if (s == -1)
		err(1, "Bad socket");

	/* XXX make sure the server started alright */
	for (i = 0; i < 10; i++) {
		r = connect(s, (struct sockaddr *)sun, sizeof(*sun));
		if (r == 0) {
			problem = check_id(s);
			if (problem)
				errx(1, "%s", problem);
			exit(0);
		}
		sleep(5);
	}
	errx(1, "Could not connect after 10 tries");
}


void
server(struct sockaddr_un *sun)
{
	int s, fd;
	struct sockaddr_storage client_addr;
	socklen_t client_len;
	char *problem;

	s = socket(AF_UNIX, SOCK_STREAM, 0);
	if (s == -1)
		err(1, "Bad socket");

	if (bind(s, (struct sockaddr *)sun, sizeof(*sun)) != 0)
		err(1, "bind");
	if (listen(s, 5) != 0) {
		int saved_errno = errno;
		unlink(path);
		rmdir(dir);
		errc(1, saved_errno, "listen");
	}
	fd = accept(s, (struct sockaddr *)&client_addr, &client_len);
	if (fd == -1) {
		int saved_errno = errno;
		unlink(path);
		rmdir(dir);
		errc(1, saved_errno, "accept");
	}
	problem = check_id(fd);
	if (problem)  {
		unlink(path);
		rmdir(dir);
		errx(1, "%s", problem);
	}
	unlink(path);
	rmdir(dir);
}



int
main()
{
	pid_t pid;
	struct sockaddr_un sun;
	char dir_template[] = "/tmp/peer.XXXXXX";

	dir = mkdtemp(dir_template);
	if (!dir)
		err(1, "mkdtemp");
	snprintf(path, sizeof path, "%s/%s", dir, "socket");

	memset(&sun, 0, sizeof(struct sockaddr_un));
	if (strlcpy(sun.sun_path, path, sizeof(sun.sun_path)) >=
	    sizeof(sun.sun_path))
		errx(1, "Memory error");
	sun.sun_family = AF_UNIX;

	/* let's make those two rendez-vous, a bit artificial */
	pid = fork();
	if (pid == -1)
		err(1, "can't fork");
	if (pid == 0) {
		client(&sun);
		exit(0);
	} else {
		int status;

		server(&sun);
		waitpid(pid, &status, 0);
		if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
			printf("getpeereid test okay\n");
			exit(0);
		} else {
			errx(1, "Problem with child");
		}
	}
}
