// SPDX-License-Identifier: GPL-2.0-or-later
/* Real Time Clock Driver Test
 *	by: Benjamin Gaignard (benjamin.gaignard@linaro.org)
 *
 * To build
 *	gcc rtctest_setdate.c -o rtctest_setdate
 */

#include <stdio.h>
#include <linux/rtc.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

static const char default_time[] = "00:00:00";

int main(int argc, char **argv)
{
	int fd, retval;
	struct rtc_time new, current;
	const char *rtc, *date;
	const char *time = default_time;

	switch (argc) {
	case 4:
		time = argv[3];
		/* FALLTHROUGH */
	case 3:
		date = argv[2];
		rtc = argv[1];
		break;
	default:
		fprintf(stderr, "usage: rtctest_setdate <rtcdev> <DD-MM-YYYY> [HH:MM:SS]\n");
		return 1;
	}

	fd = open(rtc, O_RDONLY);
	if (fd == -1) {
		perror(rtc);
		exit(errno);
	}

	sscanf(date, "%d-%d-%d", &new.tm_mday, &new.tm_mon, &new.tm_year);
	new.tm_mon -= 1;
	new.tm_year -= 1900;
	sscanf(time, "%d:%d:%d", &new.tm_hour, &new.tm_min, &new.tm_sec);

	fprintf(stderr, "Test will set RTC date/time to %d-%d-%d, %02d:%02d:%02d.\n",
		new.tm_mday, new.tm_mon + 1, new.tm_year + 1900,
		new.tm_hour, new.tm_min, new.tm_sec);

	/* Write the new date in RTC */
	retval = ioctl(fd, RTC_SET_TIME, &new);
	if (retval == -1) {
		perror("RTC_SET_TIME ioctl");
		close(fd);
		exit(errno);
	}

	/* Read back */
	retval = ioctl(fd, RTC_RD_TIME, &current);
	if (retval == -1) {
		perror("RTC_RD_TIME ioctl");
		exit(errno);
	}

	fprintf(stderr, "\n\nCurrent RTC date/time is %d-%d-%d, %02d:%02d:%02d.\n",
		current.tm_mday, current.tm_mon + 1, current.tm_year + 1900,
		current.tm_hour, current.tm_min, current.tm_sec);

	close(fd);
	return 0;
}
