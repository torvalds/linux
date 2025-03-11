// SPDX-License-Identifier: GPL-2.0
/*
 * Real Time Clock Driver Test Program
 *
 * Copyright (c) 2018 Alexandre Belloni <alexandre.belloni@bootlin.com>
 */

#include <errno.h>
#include <fcntl.h>
#include <linux/rtc.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "../kselftest_harness.h"

#define NUM_UIE 3
#define ALARM_DELTA 3
#define READ_LOOP_DURATION_SEC 30
#define READ_LOOP_SLEEP_MS 11

static char *rtc_file = "/dev/rtc0";

enum rtc_alarm_state {
	RTC_ALARM_UNKNOWN,
	RTC_ALARM_ENABLED,
	RTC_ALARM_DISABLED,
};

FIXTURE(rtc) {
	int fd;
};

FIXTURE_SETUP(rtc) {
	self->fd = open(rtc_file, O_RDONLY);
}

FIXTURE_TEARDOWN(rtc) {
	close(self->fd);
}

TEST_F(rtc, date_read) {
	int rc;
	struct rtc_time rtc_tm;

	if (self->fd == -1 && errno == ENOENT)
		SKIP(return, "Skipping test since %s does not exist", rtc_file);
	ASSERT_NE(-1, self->fd);

	/* Read the RTC time/date */
	rc = ioctl(self->fd, RTC_RD_TIME, &rtc_tm);
	ASSERT_NE(-1, rc);

	TH_LOG("Current RTC date/time is %02d/%02d/%02d %02d:%02d:%02d.",
	       rtc_tm.tm_mday, rtc_tm.tm_mon + 1, rtc_tm.tm_year + 1900,
	       rtc_tm.tm_hour, rtc_tm.tm_min, rtc_tm.tm_sec);
}

static time_t rtc_time_to_timestamp(struct rtc_time *rtc_time)
{
	struct tm tm_time = {
	       .tm_sec = rtc_time->tm_sec,
	       .tm_min = rtc_time->tm_min,
	       .tm_hour = rtc_time->tm_hour,
	       .tm_mday = rtc_time->tm_mday,
	       .tm_mon = rtc_time->tm_mon,
	       .tm_year = rtc_time->tm_year,
	};

	return mktime(&tm_time);
}

static void nanosleep_with_retries(long ns)
{
	struct timespec req = {
		.tv_sec = 0,
		.tv_nsec = ns,
	};
	struct timespec rem;

	while (nanosleep(&req, &rem) != 0) {
		req.tv_sec = rem.tv_sec;
		req.tv_nsec = rem.tv_nsec;
	}
}

static enum rtc_alarm_state get_rtc_alarm_state(int fd)
{
	struct rtc_param param = { 0 };
	int rc;

	/* Validate kernel reflects unsupported RTC alarm state */
	param.param = RTC_PARAM_FEATURES;
	param.index = 0;
	rc = ioctl(fd, RTC_PARAM_GET, &param);
	if (rc < 0)
		return RTC_ALARM_UNKNOWN;

	if ((param.uvalue & _BITUL(RTC_FEATURE_ALARM)) == 0)
		return RTC_ALARM_DISABLED;

	return RTC_ALARM_ENABLED;
}

TEST_F_TIMEOUT(rtc, date_read_loop, READ_LOOP_DURATION_SEC + 2) {
	int rc;
	long iter_count = 0;
	struct rtc_time rtc_tm;
	time_t start_rtc_read, prev_rtc_read;

	if (self->fd == -1 && errno == ENOENT)
		SKIP(return, "Skipping test since %s does not exist", rtc_file);
	ASSERT_NE(-1, self->fd);

	TH_LOG("Continuously reading RTC time for %ds (with %dms breaks after every read).",
	       READ_LOOP_DURATION_SEC, READ_LOOP_SLEEP_MS);

	rc = ioctl(self->fd, RTC_RD_TIME, &rtc_tm);
	ASSERT_NE(-1, rc);
	start_rtc_read = rtc_time_to_timestamp(&rtc_tm);
	prev_rtc_read = start_rtc_read;

	do  {
		time_t rtc_read;

		rc = ioctl(self->fd, RTC_RD_TIME, &rtc_tm);
		ASSERT_NE(-1, rc);

		rtc_read = rtc_time_to_timestamp(&rtc_tm);
		/* Time should not go backwards */
		ASSERT_LE(prev_rtc_read, rtc_read);
		/* Time should not increase more then 1s at a time */
		ASSERT_GE(prev_rtc_read + 1, rtc_read);

		/* Sleep 11ms to avoid killing / overheating the RTC */
		nanosleep_with_retries(READ_LOOP_SLEEP_MS * 1000000);

		prev_rtc_read = rtc_read;
		iter_count++;
	} while (prev_rtc_read <= start_rtc_read + READ_LOOP_DURATION_SEC);

	TH_LOG("Performed %ld RTC time reads.", iter_count);
}

TEST_F_TIMEOUT(rtc, uie_read, NUM_UIE + 2) {
	int i, rc, irq = 0;
	unsigned long data;

	if (self->fd == -1 && errno == ENOENT)
		SKIP(return, "Skipping test since %s does not exist", rtc_file);
	ASSERT_NE(-1, self->fd);

	/* Turn on update interrupts */
	rc = ioctl(self->fd, RTC_UIE_ON, 0);
	if (rc == -1) {
		ASSERT_EQ(EINVAL, errno);
		TH_LOG("skip update IRQs not supported.");
		return;
	}

	for (i = 0; i < NUM_UIE; i++) {
		/* This read will block */
		rc = read(self->fd, &data, sizeof(data));
		ASSERT_NE(-1, rc);
		irq++;
	}

	EXPECT_EQ(NUM_UIE, irq);

	rc = ioctl(self->fd, RTC_UIE_OFF, 0);
	ASSERT_NE(-1, rc);
}

TEST_F(rtc, uie_select) {
	int i, rc, irq = 0;
	unsigned long data;

	if (self->fd == -1 && errno == ENOENT)
		SKIP(return, "Skipping test since %s does not exist", rtc_file);
	ASSERT_NE(-1, self->fd);

	/* Turn on update interrupts */
	rc = ioctl(self->fd, RTC_UIE_ON, 0);
	if (rc == -1) {
		ASSERT_EQ(EINVAL, errno);
		TH_LOG("skip update IRQs not supported.");
		return;
	}

	for (i = 0; i < NUM_UIE; i++) {
		struct timeval tv = { .tv_sec = 2 };
		fd_set readfds;

		FD_ZERO(&readfds);
		FD_SET(self->fd, &readfds);
		/* The select will wait until an RTC interrupt happens. */
		rc = select(self->fd + 1, &readfds, NULL, NULL, &tv);
		ASSERT_NE(-1, rc);
		ASSERT_NE(0, rc);

		/* This read won't block */
		rc = read(self->fd, &data, sizeof(unsigned long));
		ASSERT_NE(-1, rc);
		irq++;
	}

	EXPECT_EQ(NUM_UIE, irq);

	rc = ioctl(self->fd, RTC_UIE_OFF, 0);
	ASSERT_NE(-1, rc);
}

TEST_F(rtc, alarm_alm_set) {
	struct timeval tv = { .tv_sec = ALARM_DELTA + 2 };
	unsigned long data;
	struct rtc_time tm;
	fd_set readfds;
	time_t secs, new;
	int rc;
	enum rtc_alarm_state alarm_state = RTC_ALARM_UNKNOWN;

	if (self->fd == -1 && errno == ENOENT)
		SKIP(return, "Skipping test since %s does not exist", rtc_file);
	ASSERT_NE(-1, self->fd);

	alarm_state = get_rtc_alarm_state(self->fd);
	if (alarm_state == RTC_ALARM_DISABLED)
		SKIP(return, "Skipping test since alarms are not supported.");

	rc = ioctl(self->fd, RTC_RD_TIME, &tm);
	ASSERT_NE(-1, rc);

	secs = timegm((struct tm *)&tm) + ALARM_DELTA;
	gmtime_r(&secs, (struct tm *)&tm);

	rc = ioctl(self->fd, RTC_ALM_SET, &tm);
	if (rc == -1) {
		/*
		 * Report error if rtc alarm was enabled. Fallback to check ioctl
		 * error number if rtc alarm state is unknown.
		 */
		ASSERT_EQ(RTC_ALARM_UNKNOWN, alarm_state);
		ASSERT_EQ(EINVAL, errno);
		TH_LOG("skip alarms are not supported.");
		return;
	}

	rc = ioctl(self->fd, RTC_ALM_READ, &tm);
	ASSERT_NE(-1, rc);

	TH_LOG("Alarm time now set to %02d:%02d:%02d.",
	       tm.tm_hour, tm.tm_min, tm.tm_sec);

	/* Enable alarm interrupts */
	rc = ioctl(self->fd, RTC_AIE_ON, 0);
	ASSERT_NE(-1, rc);

	FD_ZERO(&readfds);
	FD_SET(self->fd, &readfds);

	rc = select(self->fd + 1, &readfds, NULL, NULL, &tv);
	ASSERT_NE(-1, rc);
	ASSERT_NE(0, rc);

	/* Disable alarm interrupts */
	rc = ioctl(self->fd, RTC_AIE_OFF, 0);
	ASSERT_NE(-1, rc);

	rc = read(self->fd, &data, sizeof(unsigned long));
	ASSERT_NE(-1, rc);
	TH_LOG("data: %lx", data);

	rc = ioctl(self->fd, RTC_RD_TIME, &tm);
	ASSERT_NE(-1, rc);

	new = timegm((struct tm *)&tm);
	ASSERT_EQ(new, secs);
}

TEST_F(rtc, alarm_wkalm_set) {
	struct timeval tv = { .tv_sec = ALARM_DELTA + 2 };
	struct rtc_wkalrm alarm = { 0 };
	struct rtc_time tm;
	unsigned long data;
	fd_set readfds;
	time_t secs, new;
	int rc;
	enum rtc_alarm_state alarm_state = RTC_ALARM_UNKNOWN;

	if (self->fd == -1 && errno == ENOENT)
		SKIP(return, "Skipping test since %s does not exist", rtc_file);
	ASSERT_NE(-1, self->fd);

	alarm_state = get_rtc_alarm_state(self->fd);
	if (alarm_state == RTC_ALARM_DISABLED)
		SKIP(return, "Skipping test since alarms are not supported.");

	rc = ioctl(self->fd, RTC_RD_TIME, &alarm.time);
	ASSERT_NE(-1, rc);

	secs = timegm((struct tm *)&alarm.time) + ALARM_DELTA;
	gmtime_r(&secs, (struct tm *)&alarm.time);

	alarm.enabled = 1;

	rc = ioctl(self->fd, RTC_WKALM_SET, &alarm);
	if (rc == -1) {
		/*
		 * Report error if rtc alarm was enabled. Fallback to check ioctl
		 * error number if rtc alarm state is unknown.
		 */
		ASSERT_EQ(RTC_ALARM_UNKNOWN, alarm_state);
		ASSERT_EQ(EINVAL, errno);
		TH_LOG("skip alarms are not supported.");
		return;
	}

	rc = ioctl(self->fd, RTC_WKALM_RD, &alarm);
	ASSERT_NE(-1, rc);

	TH_LOG("Alarm time now set to %02d/%02d/%02d %02d:%02d:%02d.",
	       alarm.time.tm_mday, alarm.time.tm_mon + 1,
	       alarm.time.tm_year + 1900, alarm.time.tm_hour,
	       alarm.time.tm_min, alarm.time.tm_sec);

	FD_ZERO(&readfds);
	FD_SET(self->fd, &readfds);

	rc = select(self->fd + 1, &readfds, NULL, NULL, &tv);
	ASSERT_NE(-1, rc);
	ASSERT_NE(0, rc);

	rc = read(self->fd, &data, sizeof(unsigned long));
	ASSERT_NE(-1, rc);

	rc = ioctl(self->fd, RTC_RD_TIME, &tm);
	ASSERT_NE(-1, rc);

	new = timegm((struct tm *)&tm);
	ASSERT_EQ(new, secs);
}

TEST_F_TIMEOUT(rtc, alarm_alm_set_minute, 65) {
	struct timeval tv = { .tv_sec = 62 };
	unsigned long data;
	struct rtc_time tm;
	fd_set readfds;
	time_t secs, new;
	int rc;
	enum rtc_alarm_state alarm_state = RTC_ALARM_UNKNOWN;

	if (self->fd == -1 && errno == ENOENT)
		SKIP(return, "Skipping test since %s does not exist", rtc_file);
	ASSERT_NE(-1, self->fd);

	alarm_state = get_rtc_alarm_state(self->fd);
	if (alarm_state == RTC_ALARM_DISABLED)
		SKIP(return, "Skipping test since alarms are not supported.");

	rc = ioctl(self->fd, RTC_RD_TIME, &tm);
	ASSERT_NE(-1, rc);

	secs = timegm((struct tm *)&tm) + 60 - tm.tm_sec;
	gmtime_r(&secs, (struct tm *)&tm);

	rc = ioctl(self->fd, RTC_ALM_SET, &tm);
	if (rc == -1) {
		/*
		 * Report error if rtc alarm was enabled. Fallback to check ioctl
		 * error number if rtc alarm state is unknown.
		 */
		ASSERT_EQ(RTC_ALARM_UNKNOWN, alarm_state);
		ASSERT_EQ(EINVAL, errno);
		TH_LOG("skip alarms are not supported.");
		return;
	}

	rc = ioctl(self->fd, RTC_ALM_READ, &tm);
	ASSERT_NE(-1, rc);

	TH_LOG("Alarm time now set to %02d:%02d:%02d.",
	       tm.tm_hour, tm.tm_min, tm.tm_sec);

	/* Enable alarm interrupts */
	rc = ioctl(self->fd, RTC_AIE_ON, 0);
	ASSERT_NE(-1, rc);

	FD_ZERO(&readfds);
	FD_SET(self->fd, &readfds);

	rc = select(self->fd + 1, &readfds, NULL, NULL, &tv);
	ASSERT_NE(-1, rc);
	ASSERT_NE(0, rc);

	/* Disable alarm interrupts */
	rc = ioctl(self->fd, RTC_AIE_OFF, 0);
	ASSERT_NE(-1, rc);

	rc = read(self->fd, &data, sizeof(unsigned long));
	ASSERT_NE(-1, rc);
	TH_LOG("data: %lx", data);

	rc = ioctl(self->fd, RTC_RD_TIME, &tm);
	ASSERT_NE(-1, rc);

	new = timegm((struct tm *)&tm);
	ASSERT_EQ(new, secs);
}

TEST_F_TIMEOUT(rtc, alarm_wkalm_set_minute, 65) {
	struct timeval tv = { .tv_sec = 62 };
	struct rtc_wkalrm alarm = { 0 };
	struct rtc_time tm;
	unsigned long data;
	fd_set readfds;
	time_t secs, new;
	int rc;
	enum rtc_alarm_state alarm_state = RTC_ALARM_UNKNOWN;

	if (self->fd == -1 && errno == ENOENT)
		SKIP(return, "Skipping test since %s does not exist", rtc_file);
	ASSERT_NE(-1, self->fd);

	alarm_state = get_rtc_alarm_state(self->fd);
	if (alarm_state == RTC_ALARM_DISABLED)
		SKIP(return, "Skipping test since alarms are not supported.");

	rc = ioctl(self->fd, RTC_RD_TIME, &alarm.time);
	ASSERT_NE(-1, rc);

	secs = timegm((struct tm *)&alarm.time) + 60 - alarm.time.tm_sec;
	gmtime_r(&secs, (struct tm *)&alarm.time);

	alarm.enabled = 1;

	rc = ioctl(self->fd, RTC_WKALM_SET, &alarm);
	if (rc == -1) {
		/*
		 * Report error if rtc alarm was enabled. Fallback to check ioctl
		 * error number if rtc alarm state is unknown.
		 */
		ASSERT_EQ(RTC_ALARM_UNKNOWN, alarm_state);
		ASSERT_EQ(EINVAL, errno);
		TH_LOG("skip alarms are not supported.");
		return;
	}

	rc = ioctl(self->fd, RTC_WKALM_RD, &alarm);
	ASSERT_NE(-1, rc);

	TH_LOG("Alarm time now set to %02d/%02d/%02d %02d:%02d:%02d.",
	       alarm.time.tm_mday, alarm.time.tm_mon + 1,
	       alarm.time.tm_year + 1900, alarm.time.tm_hour,
	       alarm.time.tm_min, alarm.time.tm_sec);

	FD_ZERO(&readfds);
	FD_SET(self->fd, &readfds);

	rc = select(self->fd + 1, &readfds, NULL, NULL, &tv);
	ASSERT_NE(-1, rc);
	ASSERT_NE(0, rc);

	rc = read(self->fd, &data, sizeof(unsigned long));
	ASSERT_NE(-1, rc);

	rc = ioctl(self->fd, RTC_RD_TIME, &tm);
	ASSERT_NE(-1, rc);

	new = timegm((struct tm *)&tm);
	ASSERT_EQ(new, secs);
}

int main(int argc, char **argv)
{
	int ret = -1;

	switch (argc) {
	case 2:
		rtc_file = argv[1];
		/* FALLTHROUGH */
	case 1:
		break;
	default:
		fprintf(stderr, "usage: %s [rtcdev]\n", argv[0]);
		return 1;
	}

	/* Run the test if rtc_file is accessible */
	if (access(rtc_file, R_OK) == 0)
		ret = test_harness_run(argc, argv);
	else
		ksft_exit_skip("[SKIP]: Cannot access rtc file %s - Exiting\n",
						rtc_file);

	return ret;
}
