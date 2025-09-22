#include <sys/stat.h>
#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#define UV_SHOULD_SUCCEED(A, B) do {					\
	if (A) {							\
		err(1, "%s:%d - %s", __FILE__, __LINE__, B);		\
	}								\
} while (0)

#define UV_SHOULD_ENOENT(A, B) do {					\
	if (A) {							\
		if (do_uv && errno != ENOENT)				\
			err(1, "%s:%d - %s", __FILE__, __LINE__, B);	\
	} else {							\
		if (do_uv)						\
			errx(1, "%s:%d - %s worked when it should not "	\
			    "have",  __FILE__, __LINE__, B);		\
	}								\
} while(0)

#define UV_SHOULD_EACCES(A, B) do {					\
	if (A) {							\
		if (do_uv && errno != EACCES)				\
			err(1, "%s:%d - %s", __FILE__, __LINE__, B);	\
	} else {							\
		if (do_uv)						\
			errx(1, "%s:%d - %s worked when it should not "	\
			    "have",  __FILE__, __LINE__, B);		\
	}								\
} while(0)

#define UV_SHOULD_EPERM(A, B) do {					\
	if (A) {							\
		if (do_uv && errno != EPERM)				\
			err(1, "%s:%d - %s", __FILE__, __LINE__, B);	\
	} else {							\
		if (do_uv)						\
			errx(1, "%s:%d - %s worked when it should not "	\
			    "have",  __FILE__, __LINE__, B);		\
	}								\
} while(0)

static char uv_dir1[] = "/tmp/uvdir1.XXXXXX"; /* unveiled */
static char uv_dir2[] = "/tmp/uvdir2.XXXXXX"; /* not unveiled */
static char uv_file1[] = "/tmp/uvfile1.XXXXXX"; /* unveiled */
static char uv_file2[] = "/tmp/uvfile2.XXXXXX"; /* not unveiled */

static int
runcompare_internal(int (*func)(int), int fail_ok)
{
	int unveil = 0, nonunveil = 0;
	int status;
	pid_t pid = fork();
	if (pid == 0)
		exit(func(0));
	status = 0;
	waitpid(pid, &status, 0);
	if (WIFEXITED(status))
		nonunveil = WEXITSTATUS(status);
	if (WIFSIGNALED(status)) {
		printf("[FAIL] nonunveil exited with signal %d\n",
		    WTERMSIG(status));
		goto fail;
	}
	pid = fork();
	if (pid == 0)
		exit(func(1));
	status = 0;
	waitpid(pid, &status, 0);
	if (WIFEXITED(status))
		unveil = WEXITSTATUS(status);
	if (WIFSIGNALED(status)) {
		printf("[FAIL] nonunveil exited with signal %d\n",
		    WTERMSIG(status));
		goto fail;
	}
	if (!fail_ok && (unveil || nonunveil)) {
		printf("[FAIL] unveil = %d, nonunveil = %d\n", unveil,
		    nonunveil);
		goto fail;
	}
	if (unveil == nonunveil)
		return 0;
	printf("[FAIL] unveil = %d, nonunveil = %d\n", unveil, nonunveil);
 fail:
	return 1;
}

static int
runcompare(int (*func)(int))
{
	return runcompare_internal(func, 1);
}

static void
test_setup(void)
{
	int fd1, fd2;
	char filename[256];

	UV_SHOULD_SUCCEED((mkdtemp(uv_dir1) == NULL), "mkdtmp");
	UV_SHOULD_SUCCEED((mkdtemp(uv_dir2) == NULL), "mkdtmp");
	UV_SHOULD_SUCCEED(((fd1 = mkstemp(uv_file1)) == -1), "mkstemp");
	close(fd1);
	UV_SHOULD_SUCCEED((chmod(uv_file1, S_IRWXU) == -1), "chmod");
	UV_SHOULD_SUCCEED(((fd2 = mkstemp(uv_file2)) == -1), "mkstemp");
	(void)snprintf(filename, sizeof(filename), "/%s/subdir", uv_dir1);
	UV_SHOULD_SUCCEED((mkdir(filename, 0777) == -1), "mkdir");
	(void)snprintf(filename, sizeof(filename), "/%s/subdir", uv_dir2);
	UV_SHOULD_SUCCEED((mkdir(filename, 0777) == -1), "mkdir");
	close(fd2);
}
