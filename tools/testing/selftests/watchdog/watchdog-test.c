/*
 * Watchdog Driver Test Program
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/watchdog.h>

int fd;
const char v = 'V';

/*
 * This function simply sends an IOCTL to the driver, which in turn ticks
 * the PC Watchdog card to reset its internal timer so it doesn't trigger
 * a computer reset.
 */
static void keep_alive(void)
{
	int dummy;
	int ret;

	ret = ioctl(fd, WDIOC_KEEPALIVE, &dummy);
	if (!ret)
		printf(".");
}

/*
 * The main program.  Run the program with "-d" to disable the card,
 * or "-e" to enable the card.
 */

static void term(int sig)
{
	int ret = write(fd, &v, 1);

	close(fd);
	if (ret < 0)
		printf("\nStopping watchdog ticks failed (%d)...\n", errno);
	else
		printf("\nStopping watchdog ticks...\n");
	exit(0);
}

int main(int argc, char *argv[])
{
	int flags;
	unsigned int ping_rate = 1;
	int ret;
	int i;

	setbuf(stdout, NULL);

	fd = open("/dev/watchdog", O_WRONLY);

	if (fd == -1) {
		printf("Watchdog device not enabled.\n");
		exit(-1);
	}

	for (i = 1; i < argc; i++) {
		if (!strncasecmp(argv[i], "-d", 2)) {
			flags = WDIOS_DISABLECARD;
			ret = ioctl(fd, WDIOC_SETOPTIONS, &flags);
			if (!ret)
				printf("Watchdog card disabled.\n");
		} else if (!strncasecmp(argv[i], "-e", 2)) {
			flags = WDIOS_ENABLECARD;
			ret = ioctl(fd, WDIOC_SETOPTIONS, &flags);
			if (!ret)
				printf("Watchdog card enabled.\n");
		} else if (!strncasecmp(argv[i], "-t", 2) && argv[2]) {
			flags = atoi(argv[i + 1]);
			ret = ioctl(fd, WDIOC_SETTIMEOUT, &flags);
			if (!ret)
				printf("Watchdog timeout set to %u seconds.\n", flags);
			i++;
		} else if (!strncasecmp(argv[i], "-p", 2) && argv[2]) {
			ping_rate = strtoul(argv[i + 1], NULL, 0);
			printf("Watchdog ping rate set to %u seconds.\n", ping_rate);
			i++;
		} else {
			printf("-d to disable, -e to enable, -t <n> to set "
				"the timeout,\n-p <n> to set the ping rate, and ");
			printf("run by itself to tick the card.\n");
			printf("Parameters are parsed left-to-right in real-time.\n");
			printf("Example: %s -d -t 10 -p 5 -e\n", argv[0]);
			goto end;
		}
	}

	printf("Watchdog Ticking Away!\n");

	signal(SIGINT, term);

	while (1) {
		keep_alive();
		sleep(ping_rate);
	}
end:
	ret = write(fd, &v, 1);
	if (ret < 0)
		printf("Stopping watchdog ticks failed (%d)...\n", errno);
	close(fd);
	return 0;
}
