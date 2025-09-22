/* $OpenBSD: unixsock_test.c,v 1.3 2024/03/23 01:35:57 mvs Exp $ */
/* Written by Claudio Jeker in 2011 */
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

static int
test_bind(struct sockaddr_un *sun, socklen_t slen)
{
	int s, e, r;

	s = socket(AF_UNIX, SOCK_STREAM, 0);
	if (s == -1)
		err(1, "socket");

	r = bind(s, (struct sockaddr *)sun, slen);
	e = errno;
	close(s);
	sun->sun_path[slen - 2] = '\0';
	unlink(sun->sun_path);
	errno = e;
	return r;
}

static int
test_connect(struct sockaddr_un *sun, socklen_t slen, struct sockaddr_un *b)
{
	int s, s2, e, r;

	s = socket(AF_UNIX, SOCK_STREAM, 0);
	if (s == -1)
		err(1, "socket");

	s2 = socket(AF_UNIX, SOCK_STREAM, 0);
	if (s2 == -1)
		err(1, "socket");
	if (bind(s2, (struct sockaddr *)b, sizeof(*b)) == -1)
		err(1, "bind");
	if (listen(s2, 5) == -1)
		err(1, "listen");

	r = connect(s, (struct sockaddr *)sun, slen);
	e = errno;
	close(s);
	close(s2);
	unlink(b->sun_path);
	errno = e;
	return r;
}

struct test {
	socklen_t	len;
	int		r;
} t[] = {
	{30, 0},
	{50, 0},
	{100, 0},
	{102, 0},
	{103, 0},
	{104, -1},
	{105, -1},
	{110, -1},
	{200, -1},
	{0, 0}
};

int
main(void)
{
	struct sockaddr_storage ss;
	struct sockaddr_un *sun, sun2;
	char dir_template[] = "/tmp/peer.XXXXXXXXXX";
	char aaa[] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
	    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
	    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
	int i, fail = 0;

	dir = mkdtemp(dir_template);
	if (!dir)
		err(1, "mkdtemp");
	if (chdir(dir) == -1)
		err(1, "chdir");
	snprintf(path, sizeof path, "%s/%s", dir, "socket");

	for (i = 0; t[i].len != 0; i++) {
		socklen_t slen = t[i].len;
		memset(&ss, 0xfe, sizeof(ss));
		sun = (struct sockaddr_un *)&ss;
		sun->sun_family = AF_UNIX;

		memset(&sun2, 0, sizeof(sun2));
		sun2.sun_family = AF_UNIX;

		snprintf(sun->sun_path, sizeof(ss) - 2, "%s.%.*s", path,
		    (int)(slen - strlen(path) - 1), aaa);
		snprintf(sun2.sun_path, sizeof(sun2) - 2, "%s.%.*s", "socket",
		    (int)(slen - strlen(path) - 1), aaa);

		if (test_bind(sun, slen + 2) != t[i].r) {
			warn("FAIL: bind(\"%s\") len %d", sun->sun_path,
			    slen + 2);
			fail = 1;
		}
		if (test_bind(sun, slen + 3) != t[i].r) {
			warn("FAIL2: bind(\"%s\") len %d", sun->sun_path,
			     slen + 3);
			fail = 1;
		}
		if (sizeof(*sun) >= slen + 2 &&
		    test_bind(sun, sizeof(*sun)) != t[i].r) {
			warn("FAIL3: bind(\"%s\") len %zd", sun->sun_path,
			    sizeof(*sun));
			fail = 1;
		}
		sun->sun_path[slen] = 'a';
		if (test_bind(sun, slen + 2) != t[i].r) {
			warn("FAIL4: bind(\"%.*s\") len %d no-NUL",
			     (int)(slen + 2), sun->sun_path, slen + 2);
			fail = 1;
		}
		sun->sun_path[slen] = '\0';

		if (test_connect(sun, slen + 2, &sun2) != t[i].r) {
			warn("FAIL: connect(\"%s\") len %d", sun->sun_path,
			    slen + 2);
			fail = 1;
		}
		if (test_connect(sun, slen + 3, &sun2) != t[i].r) {
			warn("FAIL2: connect(\"%s\") len %d", sun->sun_path,
			    slen + 3);
			fail = 1;
		}
		if (sizeof(*sun) >= slen + 2 &&
		    test_connect(sun, sizeof(*sun), &sun2) != t[i].r) {
			warn("FAIL3: connect(\"%s\") len %zd", sun->sun_path,
			    sizeof(*sun));
			fail = 1;
		}
		sun->sun_path[slen] = 'a';
		if (test_connect(sun, slen + 2, &sun2) != t[i].r) {
			warn("FAIL4: connect(\"%.*s\") len %d no-NUL",
			    slen + 2, sun->sun_path, slen + 2);
			fail = 1;
		}
	}
	return fail;
}
