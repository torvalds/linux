#include <sys/socket.h>
#include <sys/un.h>

#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "unveil.h"

static int
test_bind_unix_socket(int do_uv)
{
	struct sockaddr_un sun1, sun2, sun3;
	char *path1, *path2, *path3;
	int c_fd1, c_fd2, fd1, fd2, fd3;

	char uv_dir3[] = "/tmp/uvdir3.XXXXXX";

	if (asprintf(&path1, "%s/1.sock", uv_dir1) == -1)
		err(1, NULL);
	if (asprintf(&path2, "%s/2.sock", uv_dir2) == -1)
		err(1, NULL);
	if (asprintf(&path3, "%s/3.sock", uv_dir3) == -1)
		err(1, NULL);

	memset(&sun1, 0, sizeof(sun1));
	sun1.sun_family = AF_UNIX;
	strlcpy(sun1.sun_path, path1, sizeof(sun1.sun_path));

	memset(&sun2, 0, sizeof(sun2));
	sun2.sun_family = AF_UNIX;
	strlcpy(sun2.sun_path, path2, sizeof(sun2.sun_path));

	memset(&sun3, 0, sizeof(sun3));
	sun3.sun_family = AF_UNIX;
	strlcpy(sun3.sun_path, path3, sizeof(sun3.sun_path));

	if (unlink(path1) == -1)
		if (errno != ENOENT) {
			warn("%s: unlink %s", __func__, path1);
			return -1;
		}
	if (unlink(path2) == -1)
		if (errno != ENOENT) {
			warn("%s: unlink %s", __func__, path2);
			return -1;
		}
	if (unlink(path3) == -1)
		if (errno != ENOENT) {
			warn("%s: unlink %s", __func__, path3);
			return -1;
		}

	if (do_uv) {
		printf("testing bind and connect on unix socket\n");
		/* printf("testing bind on unix socket %s and %s\n", path1, path2); */
		if (unveil(uv_dir1, "wc") == -1) /* both bind and connect work */
			err(1, "unveil");
		if (unveil(uv_dir2, "c") == -1) /*  bind works, connect fails */
			err(1, "unveil");
		if (unveil(uv_dir3, "") == -1) /* no bind, dont test anything else */
			err(1, "unveil");
	}

	if ((fd1 = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
		err(1, "%s: socket", __func__);
	UV_SHOULD_SUCCEED(
	    (bind(fd1, (struct sockaddr *)&sun1, sizeof(sun1)) == -1), "bind");
	if (listen(fd1, 5) == -1)
		err(1, "%s: listen", __func__);

	if ((fd2 = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
		err(1, "%s: socket", __func__);
	UV_SHOULD_SUCCEED(
	    (bind(fd2, (struct sockaddr *)&sun2, sizeof(sun2)) == -1), "bind");
	if (listen(fd2, 5) == -1)
		err(1, "%s: listen", __func__);

	if ((fd3 = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
		err(1, "%s: socket", __func__);
	UV_SHOULD_ENOENT(
	    (bind(fd3, (struct sockaddr *)&sun3, sizeof(sun3)) == -1), "bind");

	/* Connect to control socket. */

	if ((c_fd1 = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
		err(1, "socket");
	UV_SHOULD_SUCCEED(
	    (connect(c_fd1, (struct sockaddr *)&sun1, sizeof(sun1)) == -1),
	    "connect");

	if ((c_fd2 = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
		err(1, "socket");
	UV_SHOULD_EACCES(
	    (connect(c_fd2, (struct sockaddr *)&sun2, sizeof(sun2)) == -1),
	    "connect");

	close(fd1);
	close(c_fd1);
	close(fd2);
	close(c_fd2);
	return 0;
}

int
main(void)
{
	int failures = 0;

	test_setup();

	failures += runcompare(test_bind_unix_socket);
	exit(failures);
}
