// SPDX-License-Identifier: GPL-2.0
/*
 * Real Time Clock Periodic Interrupt test program
 *
 * Since commit 6610e0893b8bc ("RTC: Rework RTC code to use timerqueue for
 * events"), PIE are completely handled using hrtimers, without actually using
 * any underlying hardware RTC.
 *
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

#include "../kselftest.h"

/*
 * This expects the new RTC class driver framework, working with
 * clocks that will often not be clones of what the PC-AT had.
 * Use the command line to specify another RTC if you need one.
 */
static const char default_rtc[] = "/dev/rtc0";

int main(int argc, char **argv)
{
	int i, fd, retval;
	unsigned long tmp, data, old_pie_rate;
	const char *rtc = default_rtc;
	struct timeval start, end, diff;

	switch (argc) {
	case 2:
		rtc = argv[1];
		break;
	case 1:
		fd = open(default_rtc, O_RDONLY);
		if (fd == -1) {
			printf("Default RTC %s does not exist. Test Skipped!\n", default_rtc);
			exit(KSFT_SKIP);
		}
		close(fd);
		break;
	default:
		fprintf(stderr, "usage:  rtctest [rtcdev] [d]\n");
		return 1;
	}

	fd = open(rtc, O_RDONLY);

	if (fd ==  -1) {
		perror(rtc);
		exit(errno);
	}

	/* Read periodic IRQ rate */
	retval = ioctl(fd, RTC_IRQP_READ, &old_pie_rate);
	if (retval == -1) {
		/* not all RTCs support periodic IRQs */
		if (errno == EINVAL) {
			fprintf(stderr, "\nNo periodic IRQ support\n");
			goto done;
		}
		perror("RTC_IRQP_READ ioctl");
		exit(errno);
	}
	fprintf(stderr, "\nPeriodic IRQ rate is %ldHz.\n", old_pie_rate);

	fprintf(stderr, "Counting 20 interrupts at:");
	fflush(stderr);

	/* The frequencies 128Hz, 256Hz, ... 8192Hz are only allowed for root. */
	for (tmp=2; tmp<=64; tmp*=2) {

		retval = ioctl(fd, RTC_IRQP_SET, tmp);
		if (retval == -1) {
			/* not all RTCs can change their periodic IRQ rate */
			if (errno == EINVAL) {
				fprintf(stderr,
					"\n...Periodic IRQ rate is fixed\n");
				goto done;
			}
			perror("RTC_IRQP_SET ioctl");
			exit(errno);
		}

		fprintf(stderr, "\n%ldHz:\t", tmp);
		fflush(stderr);

		/* Enable periodic interrupts */
		retval = ioctl(fd, RTC_PIE_ON, 0);
		if (retval == -1) {
			perror("RTC_PIE_ON ioctl");
			exit(errno);
		}

		for (i=1; i<21; i++) {
			gettimeofday(&start, NULL);
			/* This blocks */
			retval = read(fd, &data, sizeof(unsigned long));
			if (retval == -1) {
				perror("read");
				exit(errno);
			}
			gettimeofday(&end, NULL);
			timersub(&end, &start, &diff);
			if (diff.tv_sec > 0 ||
			    diff.tv_usec > ((1000000L / tmp) * 1.10)) {
				fprintf(stderr, "\nPIE delta error: %ld.%06ld should be close to 0.%06ld\n",
				       diff.tv_sec, diff.tv_usec,
				       (1000000L / tmp));
				fflush(stdout);
				exit(-1);
			}

			fprintf(stderr, " %d",i);
			fflush(stderr);
		}

		/* Disable periodic interrupts */
		retval = ioctl(fd, RTC_PIE_OFF, 0);
		if (retval == -1) {
			perror("RTC_PIE_OFF ioctl");
			exit(errno);
		}
	}

done:
	ioctl(fd, RTC_IRQP_SET, old_pie_rate);

	fprintf(stderr, "\n\n\t\t\t *** Test complete ***\n");

	close(fd);

	return 0;
}
