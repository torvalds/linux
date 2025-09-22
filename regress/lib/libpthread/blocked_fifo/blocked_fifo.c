/*	$OpenBSD: blocked_fifo.c,v 1.2 2012/07/08 11:35:37 guenther Exp $	*/
/*
 * Copyright (c) 2012 Owain G. Ainsworth <oga@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Test fifo opening blocking the file descriptor table and thus all other
 * opens in the process.
 * Symptoms are that the main thread will sleep on fifor (in the fifo open) and
 * the deadlocker on fdlock (in open of any other file).
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include "test.h"

#define FIFO	"pthread.fifo"
#define FILE	"/etc/services"	/* just any file to deadlock on */

int	free_fd, expected_fd;

static void *
deadlock_detector(void *arg)
{
	sleep(10);
	unlink(FIFO);
	PANIC("deadlock detected");
}

static void *
fifo_deadlocker(void *arg)
{
	int	fd;

	/* let other fifo open start */
	sleep(3);

	/* open a random temporary file, if we don't deadlock this'll succeed */
	CHECKe(fd = open(FILE, O_RDONLY));
	CHECKe(close(fd));

	/* open fifo to unblock other thread */
	CHECKe(fd = open(FIFO, O_WRONLY));
	CHECKe(write(fd, "test", 4));
	CHECKe(close(fd));

	return ((caddr_t)NULL + errno);
}

static void *
fifo_closer(void *arg)
{
	int	fd;

	/* let other fifo open start */
	sleep(3);

	/* open a random temporary file and dup2 it over the FIFO */
	CHECKe(fd = open(FILE, O_RDONLY));
	CHECKe(dup2(fd, expected_fd));
	CHECKe(close(fd));
	CHECKe(close(expected_fd));

	/* open fifo to unblock other thread */
	CHECKe(fd = open(FIFO, O_WRONLY));
	CHECKe(write(fd, "test", 4));
	CHECKe(close(fd));

	return ((caddr_t)NULL + errno);
}

int
main(int argc, char *argv[])
{
	pthread_t	 test_thread, deadlock_finder;
	struct stat	 st;
	char		 buf[5];
	int		 fd;
	ssize_t		 rlen;

	unlink(FIFO);
	CHECKe(mkfifo(FIFO, S_IRUSR | S_IWUSR));

	CHECKr(pthread_create(&deadlock_finder, NULL,
	    deadlock_detector, NULL));

	/*
	 * Verify that other threads can still do fd-table operations
	 * while a thread is blocked opening a FIFO
	 */
	CHECKr(pthread_create(&test_thread, NULL, fifo_deadlocker, NULL));

	/* Open fifo (this will sleep until we have readers) */
	CHECKe(fd = open(FIFO, O_RDONLY));

	CHECKe(fstat(fd, &st));
	ASSERT(S_ISFIFO(st.st_mode));
	CHECKe(rlen = read(fd, buf, sizeof buf));
	ASSERT(rlen == 4);

	CHECKr(pthread_join(test_thread, NULL));

	CHECKe(close(fd));


	/*
	 * Verify that if a thread is blocked opening a FIFO and another
	 * thread targets the half-open fd with dup2 that it doesn't blow up.
	 */
	CHECKr(pthread_create(&test_thread, NULL, fifo_closer, NULL));

	free_fd = open("/dev/null", O_RDONLY);
	expected_fd = dup(free_fd);
	close(expected_fd);

	/* Open fifo (this will sleep until we have readers) */
	CHECKe(fd = open(FIFO, O_RDONLY));

	ASSERT(fd == expected_fd);
	ASSERT(close(fd) == -1);
	ASSERT(errno == EBADF);

	CHECKr(pthread_join(test_thread, NULL));

	CHECKe(close(free_fd));


	/* clean up */
	unlink(FIFO);

	SUCCEED;
}
