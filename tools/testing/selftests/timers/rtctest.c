/*
 *      Real Time Clock Driver Test/Example Program
 *
 *      Compile with:
 *		     gcc -s -Wall -Wstrict-prototypes rtctest.c -o rtctest
 *
 *      Copyright (C) 1996, Paul Gortmaker.
 *
 *      Released under the GNU General Public License, version 2,
 *      included herein by reference.
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

#ifndef ARRAY_SIZE
# define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

/*
 * This expects the new RTC class driver framework, working with
 * clocks that will often not be clones of what the PC-AT had.
 * Use the command line to specify another RTC if you need one.
 */
static const char default_rtc[] = "/dev/rtc0";

static struct rtc_time cutoff_dates[] = {
	{
		.tm_year = 70, /* 1970 -1900 */
		.tm_mday = 1,
	},
	/* signed time_t 19/01/2038 3:14:08 */
	{
		.tm_year = 138,
		.tm_mday = 19,
	},
	{
		.tm_year = 138,
		.tm_mday = 20,
	},
	{
		.tm_year = 199, /* 2099 -1900 */
		.tm_mday = 1,
	},
	{
		.tm_year = 200, /* 2100 -1900 */
		.tm_mday = 1,
	},
	/* unsigned time_t 07/02/2106 7:28:15*/
	{
		.tm_year = 205,
		.tm_mon = 1,
		.tm_mday = 7,
	},
	{
		.tm_year = 206,
		.tm_mon = 1,
		.tm_mday = 8,
	},
	/* signed time on 64bit in nanoseconds 12/04/2262 01:47:16*/
	{
		.tm_year = 362,
		.tm_mon = 3,
		.tm_mday = 12,
	},
	{
		.tm_year = 362, /* 2262 -1900 */
		.tm_mon = 3,
		.tm_mday = 13,
	},
};

static int compare_dates(struct rtc_time *a, struct rtc_time *b)
{
	if (a->tm_year != b->tm_year ||
	    a->tm_mon != b->tm_mon ||
	    a->tm_mday != b->tm_mday ||
	    a->tm_hour != b->tm_hour ||
	    a->tm_min != b->tm_min ||
	    ((b->tm_sec - a->tm_sec) > 1))
		return 1;

	return 0;
}

int main(int argc, char **argv)
{
	int i, fd, retval, irqcount = 0, dangerous = 0;
	unsigned long tmp, data;
	struct rtc_time rtc_tm;
	const char *rtc = default_rtc;
	struct timeval start, end, diff;

	switch (argc) {
	case 3:
		if (*argv[2] == 'd')
			dangerous = 1;
	case 2:
		rtc = argv[1];
		/* FALLTHROUGH */
	case 1:
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

	fprintf(stderr, "\n\t\t\tRTC Driver Test Example.\n\n");

	/* Turn on update interrupts (one per second) */
	retval = ioctl(fd, RTC_UIE_ON, 0);
	if (retval == -1) {
		if (errno == EINVAL) {
			fprintf(stderr,
				"\n...Update IRQs not supported.\n");
			goto test_READ;
		}
		perror("RTC_UIE_ON ioctl");
		exit(errno);
	}

	fprintf(stderr, "Counting 5 update (1/sec) interrupts from reading %s:",
			rtc);
	fflush(stderr);
	for (i=1; i<6; i++) {
		/* This read will block */
		retval = read(fd, &data, sizeof(unsigned long));
		if (retval == -1) {
			perror("read");
			exit(errno);
		}
		fprintf(stderr, " %d",i);
		fflush(stderr);
		irqcount++;
	}

	fprintf(stderr, "\nAgain, from using select(2) on /dev/rtc:");
	fflush(stderr);
	for (i=1; i<6; i++) {
		struct timeval tv = {5, 0};     /* 5 second timeout on select */
		fd_set readfds;

		FD_ZERO(&readfds);
		FD_SET(fd, &readfds);
		/* The select will wait until an RTC interrupt happens. */
		retval = select(fd+1, &readfds, NULL, NULL, &tv);
		if (retval == -1) {
		        perror("select");
		        exit(errno);
		}
		/* This read won't block unlike the select-less case above. */
		retval = read(fd, &data, sizeof(unsigned long));
		if (retval == -1) {
		        perror("read");
		        exit(errno);
		}
		fprintf(stderr, " %d",i);
		fflush(stderr);
		irqcount++;
	}

	/* Turn off update interrupts */
	retval = ioctl(fd, RTC_UIE_OFF, 0);
	if (retval == -1) {
		perror("RTC_UIE_OFF ioctl");
		exit(errno);
	}

test_READ:
	/* Read the RTC time/date */
	retval = ioctl(fd, RTC_RD_TIME, &rtc_tm);
	if (retval == -1) {
		perror("RTC_RD_TIME ioctl");
		exit(errno);
	}

	fprintf(stderr, "\n\nCurrent RTC date/time is %d-%d-%d, %02d:%02d:%02d.\n",
		rtc_tm.tm_mday, rtc_tm.tm_mon + 1, rtc_tm.tm_year + 1900,
		rtc_tm.tm_hour, rtc_tm.tm_min, rtc_tm.tm_sec);

	/* Set the alarm to 5 sec in the future, and check for rollover */
	rtc_tm.tm_sec += 5;
	if (rtc_tm.tm_sec >= 60) {
		rtc_tm.tm_sec %= 60;
		rtc_tm.tm_min++;
	}
	if (rtc_tm.tm_min == 60) {
		rtc_tm.tm_min = 0;
		rtc_tm.tm_hour++;
	}
	if (rtc_tm.tm_hour == 24)
		rtc_tm.tm_hour = 0;

	retval = ioctl(fd, RTC_ALM_SET, &rtc_tm);
	if (retval == -1) {
		if (errno == EINVAL) {
			fprintf(stderr,
				"\n...Alarm IRQs not supported.\n");
			goto test_PIE;
		}

		perror("RTC_ALM_SET ioctl");
		exit(errno);
	}

	/* Read the current alarm settings */
	retval = ioctl(fd, RTC_ALM_READ, &rtc_tm);
	if (retval == -1) {
		perror("RTC_ALM_READ ioctl");
		exit(errno);
	}

	fprintf(stderr, "Alarm time now set to %02d:%02d:%02d.\n",
		rtc_tm.tm_hour, rtc_tm.tm_min, rtc_tm.tm_sec);

	/* Enable alarm interrupts */
	retval = ioctl(fd, RTC_AIE_ON, 0);
	if (retval == -1) {
		if (errno == EINVAL) {
			fprintf(stderr,
				"\n...Alarm IRQs not supported.\n");
			goto test_PIE;
		}

		perror("RTC_AIE_ON ioctl");
		exit(errno);
	}

	fprintf(stderr, "Waiting 5 seconds for alarm...");
	fflush(stderr);
	/* This blocks until the alarm ring causes an interrupt */
	retval = read(fd, &data, sizeof(unsigned long));
	if (retval == -1) {
		perror("read");
		exit(errno);
	}
	irqcount++;
	fprintf(stderr, " okay. Alarm rang.\n");

	/* Disable alarm interrupts */
	retval = ioctl(fd, RTC_AIE_OFF, 0);
	if (retval == -1) {
		perror("RTC_AIE_OFF ioctl");
		exit(errno);
	}

test_PIE:
	/* Read periodic IRQ rate */
	retval = ioctl(fd, RTC_IRQP_READ, &tmp);
	if (retval == -1) {
		/* not all RTCs support periodic IRQs */
		if (errno == EINVAL) {
			fprintf(stderr, "\nNo periodic IRQ support\n");
			goto test_DATE;
		}
		perror("RTC_IRQP_READ ioctl");
		exit(errno);
	}
	fprintf(stderr, "\nPeriodic IRQ rate is %ldHz.\n", tmp);

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
				goto test_DATE;
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
			irqcount++;
		}

		/* Disable periodic interrupts */
		retval = ioctl(fd, RTC_PIE_OFF, 0);
		if (retval == -1) {
			perror("RTC_PIE_OFF ioctl");
			exit(errno);
		}
	}

test_DATE:
	if (!dangerous)
		goto done;

	fprintf(stderr, "\nTesting problematic dates\n");

	for (i = 0; i < ARRAY_SIZE(cutoff_dates); i++) {
		struct rtc_time current;

		/* Write the new date in RTC */
		retval = ioctl(fd, RTC_SET_TIME, &cutoff_dates[i]);
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

		if(compare_dates(&cutoff_dates[i], &current)) {
			fprintf(stderr,"Setting date %d failed\n",
			        cutoff_dates[i].tm_year + 1900);
			goto done;
		}

		cutoff_dates[i].tm_sec += 5;

		/* Write the new alarm in RTC */
		retval = ioctl(fd, RTC_ALM_SET, &cutoff_dates[i]);
		if (retval == -1) {
			perror("RTC_ALM_SET ioctl");
			close(fd);
			exit(errno);
		}

		/* Read back */
		retval = ioctl(fd, RTC_ALM_READ, &current);
		if (retval == -1) {
			perror("RTC_ALM_READ ioctl");
			exit(errno);
		}

		if(compare_dates(&cutoff_dates[i], &current)) {
			fprintf(stderr,"Setting alarm %d failed\n",
			        cutoff_dates[i].tm_year + 1900);
			goto done;
		}

		fprintf(stderr, "Setting year %d is OK \n",
			cutoff_dates[i].tm_year + 1900);
	}
done:
	fprintf(stderr, "\n\n\t\t\t *** Test complete ***\n");

	close(fd);

	return 0;
}
