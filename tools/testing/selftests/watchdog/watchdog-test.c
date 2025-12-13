// SPDX-License-Identifier: GPL-2.0
/*
* Watchdog Driver Test Program
* - Tests all ioctls
* - Tests Magic Close - CONFIG_WATCHDOG_NOWAYOUT
* - Could be tested against softdog driver on systems that
*   don't have watchdog hardware.
* - TODO:
* - Enhance test to add coverage for WDIOC_GETTEMP.
*
* Reference: Documentation/watchdog/watchdog-api.rst
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <getopt.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/watchdog.h>

#define DEFAULT_PING_RATE	1

int fd;
const char v = 'V';
static const char sopts[] = "bdehp:st:Tn:NLf:i";
static const struct option lopts[] = {
	{"bootstatus",          no_argument, NULL, 'b'},
	{"disable",             no_argument, NULL, 'd'},
	{"enable",              no_argument, NULL, 'e'},
	{"help",                no_argument, NULL, 'h'},
	{"pingrate",      required_argument, NULL, 'p'},
	{"status",              no_argument, NULL, 's'},
	{"timeout",       required_argument, NULL, 't'},
	{"gettimeout",          no_argument, NULL, 'T'},
	{"pretimeout",    required_argument, NULL, 'n'},
	{"getpretimeout",       no_argument, NULL, 'N'},
	{"gettimeleft",		no_argument, NULL, 'L'},
	{"file",          required_argument, NULL, 'f'},
	{"info",		no_argument, NULL, 'i'},
	{NULL,                  no_argument, NULL, 0x0}
};

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

static void usage(char *progname)
{
	printf("Usage: %s [options]\n", progname);
	printf(" -f, --file\t\tOpen watchdog device file\n");
	printf("\t\t\tDefault is /dev/watchdog\n");
	printf(" -i, --info\t\tShow watchdog_info\n");
	printf(" -s, --status\t\tGet status & supported features\n");
	printf(" -b, --bootstatus\tGet last boot status (Watchdog/POR)\n");
	printf(" -d, --disable\t\tTurn off the watchdog timer\n");
	printf(" -e, --enable\t\tTurn on the watchdog timer\n");
	printf(" -h, --help\t\tPrint the help message\n");
	printf(" -p, --pingrate=P\tSet ping rate to P seconds (default %d)\n",
	       DEFAULT_PING_RATE);
	printf(" -t, --timeout=T\tSet timeout to T seconds\n");
	printf(" -T, --gettimeout\tGet the timeout\n");
	printf(" -n, --pretimeout=T\tSet the pretimeout to T seconds\n");
	printf(" -N, --getpretimeout\tGet the pretimeout\n");
	printf(" -L, --gettimeleft\tGet the time left until timer expires\n");
	printf("\n");
	printf("Parameters are parsed left-to-right in real-time.\n");
	printf("Example: %s -d -t 10 -p 5 -e\n", progname);
	printf("Example: %s -t 12 -T -n 7 -N\n", progname);
}

struct wdiof_status {
	int flag;
	const char *status_str;
};

#define WDIOF_NUM_STATUS 8

static const struct wdiof_status wdiof_status[WDIOF_NUM_STATUS] = {
	{WDIOF_SETTIMEOUT,  "Set timeout (in seconds)"},
	{WDIOF_MAGICCLOSE,  "Supports magic close char"},
	{WDIOF_PRETIMEOUT,  "Pretimeout (in seconds), get/set"},
	{WDIOF_ALARMONLY,  "Watchdog triggers a management or other external alarm not a reboot"},
	{WDIOF_KEEPALIVEPING,  "Keep alive ping reply"},
	{WDIOS_DISABLECARD,  "Turn off the watchdog timer"},
	{WDIOS_ENABLECARD,  "Turn on the watchdog timer"},
	{WDIOS_TEMPPANIC,  "Kernel panic on temperature trip"},
};

static void print_status(int flags)
{
	int wdiof = 0;

	if (flags == WDIOS_UNKNOWN) {
		printf("Unknown status error from WDIOC_GETSTATUS\n");
		return;
	}

	for (wdiof = 0; wdiof < WDIOF_NUM_STATUS; wdiof++) {
		if (flags & wdiof_status[wdiof].flag)
			printf("Support/Status: %s\n",
				wdiof_status[wdiof].status_str);
	}
}

#define WDIOF_NUM_BOOTSTATUS 7

static const struct wdiof_status wdiof_bootstatus[WDIOF_NUM_BOOTSTATUS] = {
	{WDIOF_OVERHEAT, "Reset due to CPU overheat"},
	{WDIOF_FANFAULT, "Fan failed"},
	{WDIOF_EXTERN1, "External relay 1"},
	{WDIOF_EXTERN2, "External relay 2"},
	{WDIOF_POWERUNDER, "Power bad/power fault"},
	{WDIOF_CARDRESET, "Card previously reset the CPU"},
	{WDIOF_POWEROVER,  "Power over voltage"},
};

static void print_boot_status(int flags)
{
	int wdiof = 0;

	if (flags == WDIOF_UNKNOWN) {
		printf("Unknown flag error from WDIOC_GETBOOTSTATUS\n");
		return;
	}

	if (flags == 0) {
		printf("Last boot is caused by: Power-On-Reset\n");
		return;
	}

	for (wdiof = 0; wdiof < WDIOF_NUM_BOOTSTATUS; wdiof++) {
		if (flags & wdiof_bootstatus[wdiof].flag)
			printf("Last boot is caused by: %s\n",
				wdiof_bootstatus[wdiof].status_str);
	}
}

int main(int argc, char *argv[])
{
	int flags;
	unsigned int ping_rate = DEFAULT_PING_RATE;
	int ret;
	int c;
	int oneshot = 0;
	char *file = "/dev/watchdog";
	struct watchdog_info info;
	int temperature;

	setbuf(stdout, NULL);

	while ((c = getopt_long(argc, argv, sopts, lopts, NULL)) != -1) {
		if (c == 'f')
			file = optarg;
	}

	fd = open(file, O_WRONLY);

	if (fd == -1) {
		if (errno == ENOENT)
			printf("Watchdog device (%s) not found.\n", file);
		else if (errno == EACCES)
			printf("Run watchdog as root.\n");
		else
			printf("Watchdog device open failed %s\n",
				strerror(errno));
		exit(-1);
	}

	/*
	 * Validate that `file` is a watchdog device
	 */
	ret = ioctl(fd, WDIOC_GETSUPPORT, &info);
	if (ret) {
		printf("WDIOC_GETSUPPORT error '%s'\n", strerror(errno));
		close(fd);
		exit(ret);
	}

	optind = 0;

	while ((c = getopt_long(argc, argv, sopts, lopts, NULL)) != -1) {
		switch (c) {
		case 'b':
			flags = 0;
			oneshot = 1;
			ret = ioctl(fd, WDIOC_GETBOOTSTATUS, &flags);
			if (!ret)
				print_boot_status(flags);
			else
				printf("WDIOC_GETBOOTSTATUS error '%s'\n", strerror(errno));
			break;
		case 'd':
			flags = WDIOS_DISABLECARD;
			ret = ioctl(fd, WDIOC_SETOPTIONS, &flags);
			if (!ret)
				printf("Watchdog card disabled.\n");
			else {
				printf("WDIOS_DISABLECARD error '%s'\n", strerror(errno));
				oneshot = 1;
			}
			break;
		case 'e':
			flags = WDIOS_ENABLECARD;
			ret = ioctl(fd, WDIOC_SETOPTIONS, &flags);
			if (!ret)
				printf("Watchdog card enabled.\n");
			else {
				printf("WDIOS_ENABLECARD error '%s'\n", strerror(errno));
				oneshot = 1;
			}
			break;
		case 'p':
			ping_rate = strtoul(optarg, NULL, 0);
			if (!ping_rate)
				ping_rate = DEFAULT_PING_RATE;
			printf("Watchdog ping rate set to %u seconds.\n", ping_rate);
			break;
		case 's':
			flags = 0;
			oneshot = 1;
			ret = ioctl(fd, WDIOC_GETSTATUS, &flags);
			if (!ret)
				print_status(flags);
			else
				printf("WDIOC_GETSTATUS error '%s'\n", strerror(errno));
			ret = ioctl(fd, WDIOC_GETTEMP, &temperature);
			if (ret)
				printf("WDIOC_GETTEMP: '%s'\n", strerror(errno));
			else
				printf("Temperature %d\n", temperature);

			break;
		case 't':
			flags = strtoul(optarg, NULL, 0);
			ret = ioctl(fd, WDIOC_SETTIMEOUT, &flags);
			if (!ret)
				printf("Watchdog timeout set to %u seconds.\n", flags);
			else {
				printf("WDIOC_SETTIMEOUT error '%s'\n", strerror(errno));
				oneshot = 1;
			}
			break;
		case 'T':
			oneshot = 1;
			ret = ioctl(fd, WDIOC_GETTIMEOUT, &flags);
			if (!ret)
				printf("WDIOC_GETTIMEOUT returns %u seconds.\n", flags);
			else
				printf("WDIOC_GETTIMEOUT error '%s'\n", strerror(errno));
			break;
		case 'n':
			flags = strtoul(optarg, NULL, 0);
			ret = ioctl(fd, WDIOC_SETPRETIMEOUT, &flags);
			if (!ret)
				printf("Watchdog pretimeout set to %u seconds.\n", flags);
			else {
				printf("WDIOC_SETPRETIMEOUT error '%s'\n", strerror(errno));
				oneshot = 1;
			}
			break;
		case 'N':
			oneshot = 1;
			ret = ioctl(fd, WDIOC_GETPRETIMEOUT, &flags);
			if (!ret)
				printf("WDIOC_GETPRETIMEOUT returns %u seconds.\n", flags);
			else
				printf("WDIOC_GETPRETIMEOUT error '%s'\n", strerror(errno));
			break;
		case 'L':
			oneshot = 1;
			ret = ioctl(fd, WDIOC_GETTIMELEFT, &flags);
			if (!ret)
				printf("WDIOC_GETTIMELEFT returns %u seconds.\n", flags);
			else
				printf("WDIOC_GETTIMELEFT error '%s'\n", strerror(errno));
			break;
		case 'f':
			/* Handled above */
			break;
		case 'i':
			/*
			 * watchdog_info was obtained as part of file open
			 * validation. So we just show it here.
			 */
			oneshot = 1;
			printf("watchdog_info:\n");
			printf(" identity:\t\t%s\n", info.identity);
			printf(" firmware_version:\t%u\n",
			       info.firmware_version);
			print_status(info.options);
			break;

		default:
			usage(argv[0]);
			goto end;
		}
	}

	if (oneshot)
		goto end;

	/* Check if WDIOF_KEEPALIVEPING is supported */
	if (!(info.options & WDIOF_KEEPALIVEPING)) {
		printf("WDIOC_KEEPALIVE not supported by this device\n");
		goto end;
	}

	printf("Watchdog Ticking Away!\n");

	/*
	 * Register the signals
	 */
	signal(SIGINT, term);
	signal(SIGTERM, term);
	signal(SIGKILL, term);
	signal(SIGQUIT, term);

	while (1) {
		keep_alive();
		sleep(ping_rate);
	}
end:
	/*
	 * Send specific magic character 'V' just in case Magic Close is
	 * enabled to ensure watchdog gets disabled on close.
	 */
	ret = write(fd, &v, 1);
	if (ret < 0)
		printf("Stopping watchdog ticks failed (%d)...\n", errno);
	close(fd);
	return 0;
}
