#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <signal.h>

#define _SDT_HAS_SEMAPHORES 1
#include "sdt.h"

#define SEC(name) __attribute__((section(name), used))

#define BUF_SIZE 256

/* defined in urandom_read_aux.c */
void urand_read_without_sema(int iter_num, int iter_cnt, int read_sz);
/* these are coming from urandom_read_lib{1,2}.c */
void urandlib_read_with_sema(int iter_num, int iter_cnt, int read_sz);
void urandlib_read_without_sema(int iter_num, int iter_cnt, int read_sz);

unsigned short urand_read_with_sema_semaphore SEC(".probes");

static __attribute__((noinline))
void urandom_read(int fd, int count)
{
	char buf[BUF_SIZE];
	int i;

	for (i = 0; i < count; ++i) {
		read(fd, buf, BUF_SIZE);

		/* trigger USDTs defined in executable itself */
		urand_read_without_sema(i, count, BUF_SIZE);
		STAP_PROBE3(urand, read_with_sema, i, count, BUF_SIZE);

		/* trigger USDTs defined in shared lib */
		urandlib_read_without_sema(i, count, BUF_SIZE);
		urandlib_read_with_sema(i, count, BUF_SIZE);
	}
}

static volatile bool parent_ready;

static void handle_sigpipe(int sig)
{
	parent_ready = true;
}

int main(int argc, char *argv[])
{
	int fd = open("/dev/urandom", O_RDONLY);
	int count = 4;
	bool report_pid = false;

	if (fd < 0)
		return 1;

	if (argc >= 2)
		count = atoi(argv[1]);
	if (argc >= 3) {
		report_pid = true;
		/* install SIGPIPE handler to catch when parent closes their
		 * end of the pipe (on the other side of our stdout)
		 */
		signal(SIGPIPE, handle_sigpipe);
	}

	/* report PID and wait for parent process to send us "signal" by
	 * closing stdout
	 */
	if (report_pid) {
		while (!parent_ready) {
			fprintf(stdout, "%d\n", getpid());
			fflush(stdout);
		}
		/* at this point stdout is closed, parent process knows our
		 * PID and is ready to trace us
		 */
	}

	urandom_read(fd, count);

	close(fd);
	return 0;
}
