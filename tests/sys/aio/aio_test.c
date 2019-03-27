/*-
 * Copyright (c) 2004 Robert N. M. Watson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * Regression test to do some very basic AIO exercising on several types of
 * file descriptors.  Currently, the tests consist of initializing a fixed
 * size buffer with pseudo-random data, writing it to one fd using AIO, then
 * reading it from a second descriptor using AIO.  For some targets, the same
 * fd is used for write and read (i.e., file, md device), but for others the
 * operation is performed on a peer (pty, socket, fifo, etc).  For each file
 * descriptor type, several completion methods are tested.  This test program
 * does not attempt to exercise error cases or more subtle asynchronous
 * behavior, just make sure that the basic operations work on some basic object
 * types.
 */

#include <sys/param.h>
#include <sys/module.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/mdioctl.h>

#include <aio.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <libutil.h>
#include <limits.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include <atf-c.h>

#include "freebsd_test_suite/macros.h"
#include "local.h"

/*
 * GLOBAL_MAX sets the largest usable buffer size to be read and written, as
 * it sizes ac_buffer in the aio_context structure.  It is also the default
 * size for file I/O.  For other types, we use smaller blocks or we risk
 * blocking (and we run in a single process/thread so that would be bad).
 */
#define	GLOBAL_MAX	16384

#define	BUFFER_MAX	GLOBAL_MAX

/*
 * A completion function will block until the aio has completed, then return
 * the result of the aio.  errno will be set appropriately.
 */
typedef ssize_t (*completion)(struct aiocb*);

struct aio_context {
	int		 ac_read_fd, ac_write_fd;
	long		 ac_seed;
	char		 ac_buffer[GLOBAL_MAX];
	int		 ac_buflen;
	int		 ac_seconds;
};

static sem_t		completions;


/*
 * Fill a buffer given a seed that can be fed into srandom() to initialize
 * the PRNG in a repeatable manner.
 */
static void
aio_fill_buffer(char *buffer, int len, long seed)
{
	char ch;
	int i;

	srandom(seed);
	for (i = 0; i < len; i++) {
		ch = random() & 0xff;
		buffer[i] = ch;
	}
}

/*
 * Test that a buffer matches a given seed.  See aio_fill_buffer().  Return
 * (1) on a match, (0) on a mismatch.
 */
static int
aio_test_buffer(char *buffer, int len, long seed)
{
	char ch;
	int i;

	srandom(seed);
	for (i = 0; i < len; i++) {
		ch = random() & 0xff;
		if (buffer[i] != ch)
			return (0);
	}
	return (1);
}

/*
 * Initialize a testing context given the file descriptors provided by the
 * test setup.
 */
static void
aio_context_init(struct aio_context *ac, int read_fd,
    int write_fd, int buflen)
{

	ATF_REQUIRE_MSG(buflen <= BUFFER_MAX,
	    "aio_context_init: buffer too large (%d > %d)",
	    buflen, BUFFER_MAX);
	bzero(ac, sizeof(*ac));
	ac->ac_read_fd = read_fd;
	ac->ac_write_fd = write_fd;
	ac->ac_buflen = buflen;
	srandomdev();
	ac->ac_seed = random();
	aio_fill_buffer(ac->ac_buffer, buflen, ac->ac_seed);
	ATF_REQUIRE_MSG(aio_test_buffer(ac->ac_buffer, buflen,
	    ac->ac_seed) != 0, "aio_test_buffer: internal error");
}

static ssize_t
poll(struct aiocb *aio)
{
	int error;

	while ((error = aio_error(aio)) == EINPROGRESS)
		usleep(25000);
	if (error)
		return (error);
	else
		return (aio_return(aio));
}

static void
sigusr1_handler(int sig __unused)
{
	ATF_REQUIRE_EQ(0, sem_post(&completions));
}

static void
thr_handler(union sigval sv __unused)
{
	ATF_REQUIRE_EQ(0, sem_post(&completions));
}

static ssize_t
poll_signaled(struct aiocb *aio)
{
	int error;

	ATF_REQUIRE_EQ(0, sem_wait(&completions));
	error = aio_error(aio);
	switch (error) {
		case EINPROGRESS:
			errno = EINTR;
			return (-1);
		case 0:
			return (aio_return(aio));
		default:
			return (error);
	}
}

/*
 * Setup a signal handler for signal delivery tests
 * This isn't thread safe, but it's ok since ATF runs each testcase in a
 * separate process
 */
static struct sigevent*
setup_signal(void)
{
	static struct sigevent sev;

	ATF_REQUIRE_EQ(0, sem_init(&completions, false, 0));
	sev.sigev_notify = SIGEV_SIGNAL;
	sev.sigev_signo = SIGUSR1;
	ATF_REQUIRE(SIG_ERR != signal(SIGUSR1, sigusr1_handler));
	return (&sev);
}

/*
 * Setup a thread for thread delivery tests
 * This isn't thread safe, but it's ok since ATF runs each testcase in a
 * separate process
 */
static struct sigevent*
setup_thread(void)
{
	static struct sigevent sev;

	ATF_REQUIRE_EQ(0, sem_init(&completions, false, 0));
	sev.sigev_notify = SIGEV_THREAD;
	sev.sigev_notify_function = thr_handler;
	sev.sigev_notify_attributes = NULL;
	return (&sev);
}

static ssize_t
suspend(struct aiocb *aio)
{
	const struct aiocb *const iocbs[] = {aio};
	int error;

	error = aio_suspend(iocbs, 1, NULL);
	if (error == 0)
		return (aio_return(aio));
	else
		return (error);
}

static ssize_t
waitcomplete(struct aiocb *aio)
{
	struct aiocb *aiop;
	ssize_t ret;

	ret = aio_waitcomplete(&aiop, NULL);
	ATF_REQUIRE_EQ(aio, aiop);
	return (ret);
}

/*
 * Perform a simple write test of our initialized data buffer to the provided
 * file descriptor.
 */
static void
aio_write_test(struct aio_context *ac, completion comp, struct sigevent *sev)
{
	struct aiocb aio;
	ssize_t len;

	bzero(&aio, sizeof(aio));
	aio.aio_buf = ac->ac_buffer;
	aio.aio_nbytes = ac->ac_buflen;
	aio.aio_fildes = ac->ac_write_fd;
	aio.aio_offset = 0;
	if (sev)
		aio.aio_sigevent = *sev;

	if (aio_write(&aio) < 0)
		atf_tc_fail("aio_write failed: %s", strerror(errno));

	len = comp(&aio);
	if (len < 0)
		atf_tc_fail("aio failed: %s", strerror(errno));

	if (len != ac->ac_buflen)
		atf_tc_fail("aio short write (%jd)", (intmax_t)len);
}

/*
 * Perform a simple read test of our initialized data buffer from the
 * provided file descriptor.
 */
static void
aio_read_test(struct aio_context *ac, completion comp, struct sigevent *sev)
{
	struct aiocb aio;
	ssize_t len;

	bzero(ac->ac_buffer, ac->ac_buflen);
	bzero(&aio, sizeof(aio));
	aio.aio_buf = ac->ac_buffer;
	aio.aio_nbytes = ac->ac_buflen;
	aio.aio_fildes = ac->ac_read_fd;
	aio.aio_offset = 0;
	if (sev)
		aio.aio_sigevent = *sev;

	if (aio_read(&aio) < 0)
		atf_tc_fail("aio_read failed: %s", strerror(errno));

	len = comp(&aio);
	if (len < 0)
		atf_tc_fail("aio failed: %s", strerror(errno));

	ATF_REQUIRE_EQ_MSG(len, ac->ac_buflen,
	    "aio short read (%jd)", (intmax_t)len);

	if (aio_test_buffer(ac->ac_buffer, ac->ac_buflen, ac->ac_seed) == 0)
		atf_tc_fail("buffer mismatched");
}

/*
 * Series of type-specific tests for AIO.  For now, we just make sure we can
 * issue a write and then a read to each type.  We assume that once a write
 * is issued, a read can follow.
 */

/*
 * Test with a classic file.  Assumes we can create a moderate size temporary
 * file.
 */
#define	FILE_LEN	GLOBAL_MAX
#define	FILE_PATHNAME	"testfile"

static void
aio_file_test(completion comp, struct sigevent *sev)
{
	struct aio_context ac;
	int fd;

	ATF_REQUIRE_KERNEL_MODULE("aio");
	ATF_REQUIRE_UNSAFE_AIO();

	fd = open(FILE_PATHNAME, O_RDWR | O_CREAT, 0600);
	ATF_REQUIRE_MSG(fd != -1, "open failed: %s", strerror(errno));

	aio_context_init(&ac, fd, fd, FILE_LEN);
	aio_write_test(&ac, comp, sev);
	aio_read_test(&ac, comp, sev);
	close(fd);
}

ATF_TC_WITHOUT_HEAD(file_poll);
ATF_TC_BODY(file_poll, tc)
{
	aio_file_test(poll, NULL);
}

ATF_TC_WITHOUT_HEAD(file_signal);
ATF_TC_BODY(file_signal, tc)
{
	aio_file_test(poll_signaled, setup_signal());
}

ATF_TC_WITHOUT_HEAD(file_suspend);
ATF_TC_BODY(file_suspend, tc)
{
	aio_file_test(suspend, NULL);
}

ATF_TC_WITHOUT_HEAD(file_thread);
ATF_TC_BODY(file_thread, tc)
{
	aio_file_test(poll_signaled, setup_thread());
}

ATF_TC_WITHOUT_HEAD(file_waitcomplete);
ATF_TC_BODY(file_waitcomplete, tc)
{
	aio_file_test(waitcomplete, NULL);
}

#define	FIFO_LEN	256
#define	FIFO_PATHNAME	"testfifo"

static void
aio_fifo_test(completion comp, struct sigevent *sev)
{
	int error, read_fd = -1, write_fd = -1;
	struct aio_context ac;

	ATF_REQUIRE_KERNEL_MODULE("aio");
	ATF_REQUIRE_UNSAFE_AIO();

	ATF_REQUIRE_MSG(mkfifo(FIFO_PATHNAME, 0600) != -1,
	    "mkfifo failed: %s", strerror(errno));

	read_fd = open(FIFO_PATHNAME, O_RDONLY | O_NONBLOCK);
	if (read_fd == -1) {
		error = errno;
		errno = error;
		atf_tc_fail("read_fd open failed: %s",
		    strerror(errno));
	}

	write_fd = open(FIFO_PATHNAME, O_WRONLY);
	if (write_fd == -1) {
		error = errno;
		errno = error;
		atf_tc_fail("write_fd open failed: %s",
		    strerror(errno));
	}

	aio_context_init(&ac, read_fd, write_fd, FIFO_LEN);
	aio_write_test(&ac, comp, sev);
	aio_read_test(&ac, comp, sev);

	close(read_fd);
	close(write_fd);
}

ATF_TC_WITHOUT_HEAD(fifo_poll);
ATF_TC_BODY(fifo_poll, tc)
{
	aio_fifo_test(poll, NULL);
}

ATF_TC_WITHOUT_HEAD(fifo_signal);
ATF_TC_BODY(fifo_signal, tc)
{
	aio_fifo_test(poll_signaled, setup_signal());
}

ATF_TC_WITHOUT_HEAD(fifo_suspend);
ATF_TC_BODY(fifo_suspend, tc)
{
	aio_fifo_test(suspend, NULL);
}

ATF_TC_WITHOUT_HEAD(fifo_thread);
ATF_TC_BODY(fifo_thread, tc)
{
	aio_fifo_test(poll_signaled, setup_thread());
}

ATF_TC_WITHOUT_HEAD(fifo_waitcomplete);
ATF_TC_BODY(fifo_waitcomplete, tc)
{
	aio_fifo_test(waitcomplete, NULL);
}

#define	UNIX_SOCKETPAIR_LEN	256
static void
aio_unix_socketpair_test(completion comp, struct sigevent *sev)
{
	struct aio_context ac;
	struct rusage ru_before, ru_after;
	int sockets[2];

	ATF_REQUIRE_KERNEL_MODULE("aio");

	ATF_REQUIRE_MSG(socketpair(PF_UNIX, SOCK_STREAM, 0, sockets) != -1,
	    "socketpair failed: %s", strerror(errno));

	aio_context_init(&ac, sockets[0], sockets[1], UNIX_SOCKETPAIR_LEN);
	ATF_REQUIRE_MSG(getrusage(RUSAGE_SELF, &ru_before) != -1,
	    "getrusage failed: %s", strerror(errno));
	aio_write_test(&ac, comp, sev);
	ATF_REQUIRE_MSG(getrusage(RUSAGE_SELF, &ru_after) != -1,
	    "getrusage failed: %s", strerror(errno));
	ATF_REQUIRE(ru_after.ru_msgsnd == ru_before.ru_msgsnd + 1);
	ru_before = ru_after;
	aio_read_test(&ac, comp, sev);
	ATF_REQUIRE_MSG(getrusage(RUSAGE_SELF, &ru_after) != -1,
	    "getrusage failed: %s", strerror(errno));
	ATF_REQUIRE(ru_after.ru_msgrcv == ru_before.ru_msgrcv + 1);

	close(sockets[0]);
	close(sockets[1]);
}

ATF_TC_WITHOUT_HEAD(socket_poll);
ATF_TC_BODY(socket_poll, tc)
{
	aio_unix_socketpair_test(poll, NULL);
}

ATF_TC_WITHOUT_HEAD(socket_signal);
ATF_TC_BODY(socket_signal, tc)
{
	aio_unix_socketpair_test(poll_signaled, setup_signal());
}

ATF_TC_WITHOUT_HEAD(socket_suspend);
ATF_TC_BODY(socket_suspend, tc)
{
	aio_unix_socketpair_test(suspend, NULL);
}

ATF_TC_WITHOUT_HEAD(socket_thread);
ATF_TC_BODY(socket_thread, tc)
{
	aio_unix_socketpair_test(poll_signaled, setup_thread());
}

ATF_TC_WITHOUT_HEAD(socket_waitcomplete);
ATF_TC_BODY(socket_waitcomplete, tc)
{
	aio_unix_socketpair_test(waitcomplete, NULL);
}

struct aio_pty_arg {
	int	apa_read_fd;
	int	apa_write_fd;
};

#define	PTY_LEN		256
static void
aio_pty_test(completion comp, struct sigevent *sev)
{
	struct aio_context ac;
	int read_fd, write_fd;
	struct termios ts;
	int error;

	ATF_REQUIRE_KERNEL_MODULE("aio");
	ATF_REQUIRE_UNSAFE_AIO();

	ATF_REQUIRE_MSG(openpty(&read_fd, &write_fd, NULL, NULL, NULL) == 0,
	    "openpty failed: %s", strerror(errno));


	if (tcgetattr(write_fd, &ts) < 0) {
		error = errno;
		errno = error;
		atf_tc_fail("tcgetattr failed: %s", strerror(errno));
	}
	cfmakeraw(&ts);
	if (tcsetattr(write_fd, TCSANOW, &ts) < 0) {
		error = errno;
		errno = error;
		atf_tc_fail("tcsetattr failed: %s", strerror(errno));
	}
	aio_context_init(&ac, read_fd, write_fd, PTY_LEN);

	aio_write_test(&ac, comp, sev);
	aio_read_test(&ac, comp, sev);

	close(read_fd);
	close(write_fd);
}

ATF_TC_WITHOUT_HEAD(pty_poll);
ATF_TC_BODY(pty_poll, tc)
{
	aio_pty_test(poll, NULL);
}

ATF_TC_WITHOUT_HEAD(pty_signal);
ATF_TC_BODY(pty_signal, tc)
{
	aio_pty_test(poll_signaled, setup_signal());
}

ATF_TC_WITHOUT_HEAD(pty_suspend);
ATF_TC_BODY(pty_suspend, tc)
{
	aio_pty_test(suspend, NULL);
}

ATF_TC_WITHOUT_HEAD(pty_thread);
ATF_TC_BODY(pty_thread, tc)
{
	aio_pty_test(poll_signaled, setup_thread());
}

ATF_TC_WITHOUT_HEAD(pty_waitcomplete);
ATF_TC_BODY(pty_waitcomplete, tc)
{
	aio_pty_test(waitcomplete, NULL);
}

#define	PIPE_LEN	256
static void
aio_pipe_test(completion comp, struct sigevent *sev)
{
	struct aio_context ac;
	int pipes[2];

	ATF_REQUIRE_KERNEL_MODULE("aio");
	ATF_REQUIRE_UNSAFE_AIO();

	ATF_REQUIRE_MSG(pipe(pipes) != -1,
	    "pipe failed: %s", strerror(errno));

	aio_context_init(&ac, pipes[0], pipes[1], PIPE_LEN);
	aio_write_test(&ac, comp, sev);
	aio_read_test(&ac, comp, sev);

	close(pipes[0]);
	close(pipes[1]);
}

ATF_TC_WITHOUT_HEAD(pipe_poll);
ATF_TC_BODY(pipe_poll, tc)
{
	aio_pipe_test(poll, NULL);
}

ATF_TC_WITHOUT_HEAD(pipe_signal);
ATF_TC_BODY(pipe_signal, tc)
{
	aio_pipe_test(poll_signaled, setup_signal());
}

ATF_TC_WITHOUT_HEAD(pipe_suspend);
ATF_TC_BODY(pipe_suspend, tc)
{
	aio_pipe_test(suspend, NULL);
}

ATF_TC_WITHOUT_HEAD(pipe_thread);
ATF_TC_BODY(pipe_thread, tc)
{
	aio_pipe_test(poll_signaled, setup_thread());
}

ATF_TC_WITHOUT_HEAD(pipe_waitcomplete);
ATF_TC_BODY(pipe_waitcomplete, tc)
{
	aio_pipe_test(waitcomplete, NULL);
}

#define	MD_LEN		GLOBAL_MAX
#define	MDUNIT_LINK	"mdunit_link"

static void
aio_md_cleanup(void)
{
	struct md_ioctl mdio;
	int mdctl_fd, error, n, unit;
	char buf[80];

	mdctl_fd = open("/dev/" MDCTL_NAME, O_RDWR, 0);
	ATF_REQUIRE(mdctl_fd >= 0);
	n = readlink(MDUNIT_LINK, buf, sizeof(buf));
	if (n > 0) {
		if (sscanf(buf, "%d", &unit) == 1 && unit >= 0) {
			bzero(&mdio, sizeof(mdio));
			mdio.md_version = MDIOVERSION;
			mdio.md_unit = unit;
			if (ioctl(mdctl_fd, MDIOCDETACH, &mdio) == -1) {
				error = errno;
				close(mdctl_fd);
				errno = error;
				atf_tc_fail("ioctl MDIOCDETACH failed: %s",
				    strerror(errno));
			}
		}
	}
		
	close(mdctl_fd);
}

static void
aio_md_test(completion comp, struct sigevent *sev)
{
	int error, fd, mdctl_fd, unit;
	char pathname[PATH_MAX];
	struct aio_context ac;
	struct md_ioctl mdio;
	char buf[80];

	ATF_REQUIRE_KERNEL_MODULE("aio");

	mdctl_fd = open("/dev/" MDCTL_NAME, O_RDWR, 0);
	ATF_REQUIRE_MSG(mdctl_fd != -1,
	    "opening /dev/%s failed: %s", MDCTL_NAME, strerror(errno));

	bzero(&mdio, sizeof(mdio));
	mdio.md_version = MDIOVERSION;
	mdio.md_type = MD_MALLOC;
	mdio.md_options = MD_AUTOUNIT | MD_COMPRESS;
	mdio.md_mediasize = GLOBAL_MAX;
	mdio.md_sectorsize = 512;

	if (ioctl(mdctl_fd, MDIOCATTACH, &mdio) < 0) {
		error = errno;
		errno = error;
		atf_tc_fail("ioctl MDIOCATTACH failed: %s", strerror(errno));
	}
	close(mdctl_fd);

	/* Store the md unit number in a symlink for future cleanup */
	unit = mdio.md_unit;
	snprintf(buf, sizeof(buf), "%d", unit);
	ATF_REQUIRE_EQ(0, symlink(buf, MDUNIT_LINK));
	snprintf(pathname, PATH_MAX, "/dev/md%d", unit);
	fd = open(pathname, O_RDWR);
	ATF_REQUIRE_MSG(fd != -1,
	    "opening %s failed: %s", pathname, strerror(errno));

	aio_context_init(&ac, fd, fd, MD_LEN);
	aio_write_test(&ac, comp, sev);
	aio_read_test(&ac, comp, sev);
	
	close(fd);
}

ATF_TC_WITH_CLEANUP(md_poll);
ATF_TC_HEAD(md_poll, tc)
{

	atf_tc_set_md_var(tc, "require.user", "root");
}
ATF_TC_BODY(md_poll, tc)
{
	aio_md_test(poll, NULL);
}
ATF_TC_CLEANUP(md_poll, tc)
{
	aio_md_cleanup();
}

ATF_TC_WITH_CLEANUP(md_signal);
ATF_TC_HEAD(md_signal, tc)
{

	atf_tc_set_md_var(tc, "require.user", "root");
}
ATF_TC_BODY(md_signal, tc)
{
	aio_md_test(poll_signaled, setup_signal());
}
ATF_TC_CLEANUP(md_signal, tc)
{
	aio_md_cleanup();
}

ATF_TC_WITH_CLEANUP(md_suspend);
ATF_TC_HEAD(md_suspend, tc)
{

	atf_tc_set_md_var(tc, "require.user", "root");
}
ATF_TC_BODY(md_suspend, tc)
{
	aio_md_test(suspend, NULL);
}
ATF_TC_CLEANUP(md_suspend, tc)
{
	aio_md_cleanup();
}

ATF_TC_WITH_CLEANUP(md_thread);
ATF_TC_HEAD(md_thread, tc)
{

	atf_tc_set_md_var(tc, "require.user", "root");
}
ATF_TC_BODY(md_thread, tc)
{
	aio_md_test(poll_signaled, setup_thread());
}
ATF_TC_CLEANUP(md_thread, tc)
{
	aio_md_cleanup();
}

ATF_TC_WITH_CLEANUP(md_waitcomplete);
ATF_TC_HEAD(md_waitcomplete, tc)
{

	atf_tc_set_md_var(tc, "require.user", "root");
}
ATF_TC_BODY(md_waitcomplete, tc)
{
	aio_md_test(waitcomplete, NULL);
}
ATF_TC_CLEANUP(md_waitcomplete, tc)
{
	aio_md_cleanup();
}

ATF_TC_WITHOUT_HEAD(aio_large_read_test);
ATF_TC_BODY(aio_large_read_test, tc)
{
	struct aiocb cb, *cbp;
	ssize_t nread;
	size_t len;
	int fd;
#ifdef __LP64__
	int clamped;
#endif

	ATF_REQUIRE_KERNEL_MODULE("aio");
	ATF_REQUIRE_UNSAFE_AIO();

#ifdef __LP64__
	len = sizeof(clamped);
	if (sysctlbyname("debug.iosize_max_clamp", &clamped, &len, NULL, 0) ==
	    -1)
		atf_libc_error(errno, "Failed to read debug.iosize_max_clamp");
#endif

	/* Determine the maximum supported read(2) size. */
	len = SSIZE_MAX;
#ifdef __LP64__
	if (clamped)
		len = INT_MAX;
#endif

	fd = open(FILE_PATHNAME, O_RDWR | O_CREAT, 0600);
	ATF_REQUIRE_MSG(fd != -1, "open failed: %s", strerror(errno));

	unlink(FILE_PATHNAME);

	memset(&cb, 0, sizeof(cb));
	cb.aio_nbytes = len;
	cb.aio_fildes = fd;
	cb.aio_buf = NULL;
	if (aio_read(&cb) == -1)
		atf_tc_fail("aio_read() of maximum read size failed: %s",
		    strerror(errno));

	nread = aio_waitcomplete(&cbp, NULL);
	if (nread == -1)
		atf_tc_fail("aio_waitcomplete() failed: %s", strerror(errno));
	if (nread != 0)
		atf_tc_fail("aio_read() from empty file returned data: %zd",
		    nread);

	memset(&cb, 0, sizeof(cb));
	cb.aio_nbytes = len + 1;
	cb.aio_fildes = fd;
	cb.aio_buf = NULL;
	if (aio_read(&cb) == -1) {
		if (errno == EINVAL)
			goto finished;
		atf_tc_fail("aio_read() of too large read size failed: %s",
		    strerror(errno));
	}

	nread = aio_waitcomplete(&cbp, NULL);
	if (nread == -1) {
		if (errno == EINVAL)
			goto finished;
		atf_tc_fail("aio_waitcomplete() failed: %s", strerror(errno));
	}
	atf_tc_fail("aio_read() of too large read size returned: %zd", nread);

finished:
	close(fd);
}

/*
 * This tests for a bug where arriving socket data can wakeup multiple
 * AIO read requests resulting in an uncancellable request.
 */
ATF_TC_WITHOUT_HEAD(aio_socket_two_reads);
ATF_TC_BODY(aio_socket_two_reads, tc)
{
	struct ioreq {
		struct aiocb iocb;
		char buffer[1024];
	} ioreq[2];
	struct aiocb *iocb;
	unsigned i;
	int s[2];
	char c;

	ATF_REQUIRE_KERNEL_MODULE("aio");
#if __FreeBSD_version < 1100101
	aft_tc_skip("kernel version %d is too old (%d required)",
	    __FreeBSD_version, 1100101);
#endif

	ATF_REQUIRE(socketpair(PF_UNIX, SOCK_STREAM, 0, s) != -1);

	/* Queue two read requests. */
	memset(&ioreq, 0, sizeof(ioreq));
	for (i = 0; i < nitems(ioreq); i++) {
		ioreq[i].iocb.aio_nbytes = sizeof(ioreq[i].buffer);
		ioreq[i].iocb.aio_fildes = s[0];
		ioreq[i].iocb.aio_buf = ioreq[i].buffer;
		ATF_REQUIRE(aio_read(&ioreq[i].iocb) == 0);
	}

	/* Send a single byte.  This should complete one request. */
	c = 0xc3;
	ATF_REQUIRE(write(s[1], &c, sizeof(c)) == 1);

	ATF_REQUIRE(aio_waitcomplete(&iocb, NULL) == 1);

	/* Determine which request completed and verify the data was read. */
	if (iocb == &ioreq[0].iocb)
		i = 0;
	else
		i = 1;
	ATF_REQUIRE(ioreq[i].buffer[0] == c);

	i ^= 1;

	/*
	 * Try to cancel the other request.  On broken systems this
	 * will fail and the process will hang on exit.
	 */
	ATF_REQUIRE(aio_error(&ioreq[i].iocb) == EINPROGRESS);
	ATF_REQUIRE(aio_cancel(s[0], &ioreq[i].iocb) == AIO_CANCELED);

	close(s[1]);
	close(s[0]);
}

/*
 * This test ensures that aio_write() on a blocking socket of a "large"
 * buffer does not return a short completion.
 */
ATF_TC_WITHOUT_HEAD(aio_socket_blocking_short_write);
ATF_TC_BODY(aio_socket_blocking_short_write, tc)
{
	struct aiocb iocb, *iocbp;
	char *buffer[2];
	ssize_t done;
	int buffer_size, sb_size;
	socklen_t len;
	int s[2];

	ATF_REQUIRE_KERNEL_MODULE("aio");

	ATF_REQUIRE(socketpair(PF_UNIX, SOCK_STREAM, 0, s) != -1);

	len = sizeof(sb_size);
	ATF_REQUIRE(getsockopt(s[0], SOL_SOCKET, SO_RCVBUF, &sb_size, &len) !=
	    -1);
	ATF_REQUIRE(len == sizeof(sb_size));
	buffer_size = sb_size;

	ATF_REQUIRE(getsockopt(s[1], SOL_SOCKET, SO_SNDBUF, &sb_size, &len) !=
	    -1);
	ATF_REQUIRE(len == sizeof(sb_size));
	if (sb_size > buffer_size)
		buffer_size = sb_size;

	/*
	 * Use twice the size of the MAX(receive buffer, send buffer)
	 * to ensure that the write is split up into multiple writes
	 * internally.
	 */
	buffer_size *= 2;

	buffer[0] = malloc(buffer_size);
	ATF_REQUIRE(buffer[0] != NULL);
	buffer[1] = malloc(buffer_size);
	ATF_REQUIRE(buffer[1] != NULL);

	srandomdev();
	aio_fill_buffer(buffer[1], buffer_size, random());

	memset(&iocb, 0, sizeof(iocb));
	iocb.aio_fildes = s[1];
	iocb.aio_buf = buffer[1];
	iocb.aio_nbytes = buffer_size;
	ATF_REQUIRE(aio_write(&iocb) == 0);

	done = recv(s[0], buffer[0], buffer_size, MSG_WAITALL);
	ATF_REQUIRE(done == buffer_size);

	done = aio_waitcomplete(&iocbp, NULL);
	ATF_REQUIRE(iocbp == &iocb);
	ATF_REQUIRE(done == buffer_size);

	ATF_REQUIRE(memcmp(buffer[0], buffer[1], buffer_size) == 0);

	close(s[1]);
	close(s[0]);
}

/*
 * This test verifies that cancelling a partially completed socket write
 * returns a short write rather than ECANCELED.
 */
ATF_TC_WITHOUT_HEAD(aio_socket_short_write_cancel);
ATF_TC_BODY(aio_socket_short_write_cancel, tc)
{
	struct aiocb iocb, *iocbp;
	char *buffer[2];
	ssize_t done;
	int buffer_size, sb_size;
	socklen_t len;
	int s[2];

	ATF_REQUIRE_KERNEL_MODULE("aio");

	ATF_REQUIRE(socketpair(PF_UNIX, SOCK_STREAM, 0, s) != -1);

	len = sizeof(sb_size);
	ATF_REQUIRE(getsockopt(s[0], SOL_SOCKET, SO_RCVBUF, &sb_size, &len) !=
	    -1);
	ATF_REQUIRE(len == sizeof(sb_size));
	buffer_size = sb_size;

	ATF_REQUIRE(getsockopt(s[1], SOL_SOCKET, SO_SNDBUF, &sb_size, &len) !=
	    -1);
	ATF_REQUIRE(len == sizeof(sb_size));
	if (sb_size > buffer_size)
		buffer_size = sb_size;

	/*
	 * Use three times the size of the MAX(receive buffer, send
	 * buffer) for the write to ensure that the write is split up
	 * into multiple writes internally.  The recv() ensures that
	 * the write has partially completed, but a remaining size of
	 * two buffers should ensure that the write has not completed
	 * fully when it is cancelled.
	 */
	buffer[0] = malloc(buffer_size);
	ATF_REQUIRE(buffer[0] != NULL);
	buffer[1] = malloc(buffer_size * 3);
	ATF_REQUIRE(buffer[1] != NULL);

	srandomdev();
	aio_fill_buffer(buffer[1], buffer_size * 3, random());

	memset(&iocb, 0, sizeof(iocb));
	iocb.aio_fildes = s[1];
	iocb.aio_buf = buffer[1];
	iocb.aio_nbytes = buffer_size * 3;
	ATF_REQUIRE(aio_write(&iocb) == 0);

	done = recv(s[0], buffer[0], buffer_size, MSG_WAITALL);
	ATF_REQUIRE(done == buffer_size);

	ATF_REQUIRE(aio_error(&iocb) == EINPROGRESS);
	ATF_REQUIRE(aio_cancel(s[1], &iocb) == AIO_NOTCANCELED);

	done = aio_waitcomplete(&iocbp, NULL);
	ATF_REQUIRE(iocbp == &iocb);
	ATF_REQUIRE(done >= buffer_size && done <= buffer_size * 2);

	ATF_REQUIRE(memcmp(buffer[0], buffer[1], buffer_size) == 0);

	close(s[1]);
	close(s[0]);
}

/* 
 * test aio_fsync's behavior with bad inputs 
 */
ATF_TC_WITHOUT_HEAD(aio_fsync_errors);
ATF_TC_BODY(aio_fsync_errors, tc)
{
	int fd;
	struct aiocb iocb;

	ATF_REQUIRE_KERNEL_MODULE("aio");
	ATF_REQUIRE_UNSAFE_AIO();

	fd = open(FILE_PATHNAME, O_RDWR | O_CREAT, 0600);
	ATF_REQUIRE_MSG(fd != -1, "open failed: %s", strerror(errno));
	unlink(FILE_PATHNAME);

	/* aio_fsync should return EINVAL unless op is O_SYNC */
	memset(&iocb, 0, sizeof(iocb));
	iocb.aio_fildes = fd;
	ATF_CHECK_EQ(-1, aio_fsync(666, &iocb));
	ATF_CHECK_EQ(EINVAL, errno);

	/* aio_fsync should return EBADF if fd is not a valid descriptor */
	memset(&iocb, 0, sizeof(iocb));
	iocb.aio_fildes = 666;
	ATF_CHECK_EQ(-1, aio_fsync(O_SYNC, &iocb));
	ATF_CHECK_EQ(EBADF, errno);

	/* aio_fsync should return EINVAL if sigev_notify is invalid */
	memset(&iocb, 0, sizeof(iocb));
	iocb.aio_fildes = fd;
	iocb.aio_sigevent.sigev_notify = 666;
	ATF_CHECK_EQ(-1, aio_fsync(666, &iocb));
	ATF_CHECK_EQ(EINVAL, errno);
}

/*
 * This test just performs a basic test of aio_fsync().
 */
ATF_TC_WITHOUT_HEAD(aio_fsync_test);
ATF_TC_BODY(aio_fsync_test, tc)
{
	struct aiocb synccb, *iocbp;
	struct {
		struct aiocb iocb;
		bool done;
		char *buffer;
	} buffers[16];
	struct stat sb;
	ssize_t rval;
	unsigned i;
	int fd;

	ATF_REQUIRE_KERNEL_MODULE("aio");
	ATF_REQUIRE_UNSAFE_AIO();

	fd = open(FILE_PATHNAME, O_RDWR | O_CREAT, 0600);
	ATF_REQUIRE_MSG(fd != -1, "open failed: %s", strerror(errno));
	unlink(FILE_PATHNAME);

	ATF_REQUIRE(fstat(fd, &sb) == 0);
	ATF_REQUIRE(sb.st_blksize != 0);
	ATF_REQUIRE(ftruncate(fd, sb.st_blksize * nitems(buffers)) == 0);

	/*
	 * Queue several asynchronous write requests.  Hopefully this
	 * forces the aio_fsync() request to be deferred.  There is no
	 * reliable way to guarantee that however.
	 */
	srandomdev();
	for (i = 0; i < nitems(buffers); i++) {
		buffers[i].done = false;
		memset(&buffers[i].iocb, 0, sizeof(buffers[i].iocb));
		buffers[i].buffer = malloc(sb.st_blksize);
		aio_fill_buffer(buffers[i].buffer, sb.st_blksize, random());
		buffers[i].iocb.aio_fildes = fd;
		buffers[i].iocb.aio_buf = buffers[i].buffer;
		buffers[i].iocb.aio_nbytes = sb.st_blksize;
		buffers[i].iocb.aio_offset = sb.st_blksize * i;
		ATF_REQUIRE(aio_write(&buffers[i].iocb) == 0);
	}

	/* Queue the aio_fsync request. */
	memset(&synccb, 0, sizeof(synccb));
	synccb.aio_fildes = fd;
	ATF_REQUIRE(aio_fsync(O_SYNC, &synccb) == 0);

	/* Wait for requests to complete. */
	for (;;) {
	next:
		rval = aio_waitcomplete(&iocbp, NULL);
		ATF_REQUIRE(iocbp != NULL);
		if (iocbp == &synccb) {
			ATF_REQUIRE(rval == 0);
			break;
		}

		for (i = 0; i < nitems(buffers); i++) {
			if (iocbp == &buffers[i].iocb) {
				ATF_REQUIRE(buffers[i].done == false);
				ATF_REQUIRE(rval == sb.st_blksize);
				buffers[i].done = true;
				goto next;
			}
		}

		ATF_REQUIRE_MSG(false, "unmatched AIO request");
	}

	for (i = 0; i < nitems(buffers); i++)
		ATF_REQUIRE_MSG(buffers[i].done,
		    "AIO request %u did not complete", i);

	close(fd);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, file_poll);
	ATF_TP_ADD_TC(tp, file_signal);
	ATF_TP_ADD_TC(tp, file_suspend);
	ATF_TP_ADD_TC(tp, file_thread);
	ATF_TP_ADD_TC(tp, file_waitcomplete);
	ATF_TP_ADD_TC(tp, fifo_poll);
	ATF_TP_ADD_TC(tp, fifo_signal);
	ATF_TP_ADD_TC(tp, fifo_suspend);
	ATF_TP_ADD_TC(tp, fifo_thread);
	ATF_TP_ADD_TC(tp, fifo_waitcomplete);
	ATF_TP_ADD_TC(tp, socket_poll);
	ATF_TP_ADD_TC(tp, socket_signal);
	ATF_TP_ADD_TC(tp, socket_suspend);
	ATF_TP_ADD_TC(tp, socket_thread);
	ATF_TP_ADD_TC(tp, socket_waitcomplete);
	ATF_TP_ADD_TC(tp, pty_poll);
	ATF_TP_ADD_TC(tp, pty_signal);
	ATF_TP_ADD_TC(tp, pty_suspend);
	ATF_TP_ADD_TC(tp, pty_thread);
	ATF_TP_ADD_TC(tp, pty_waitcomplete);
	ATF_TP_ADD_TC(tp, pipe_poll);
	ATF_TP_ADD_TC(tp, pipe_signal);
	ATF_TP_ADD_TC(tp, pipe_suspend);
	ATF_TP_ADD_TC(tp, pipe_thread);
	ATF_TP_ADD_TC(tp, pipe_waitcomplete);
	ATF_TP_ADD_TC(tp, md_poll);
	ATF_TP_ADD_TC(tp, md_signal);
	ATF_TP_ADD_TC(tp, md_suspend);
	ATF_TP_ADD_TC(tp, md_thread);
	ATF_TP_ADD_TC(tp, md_waitcomplete);
	ATF_TP_ADD_TC(tp, aio_fsync_errors);
	ATF_TP_ADD_TC(tp, aio_fsync_test);
	ATF_TP_ADD_TC(tp, aio_large_read_test);
	ATF_TP_ADD_TC(tp, aio_socket_two_reads);
	ATF_TP_ADD_TC(tp, aio_socket_blocking_short_write);
	ATF_TP_ADD_TC(tp, aio_socket_short_write_cancel);

	return (atf_no_error());
}
