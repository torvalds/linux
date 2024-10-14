// SPDX-License-Identifier: GPL-2.0
/*
 * This test covers the functionality of userspace-driven ALSA timers. Such timers
 * are purely virtual (so they don't directly depend on the hardware), and they could be
 * created and triggered by userspace applications.
 *
 * Author: Ivan Orlov <ivan.orlov0322@gmail.com>
 */
#include "../kselftest_harness.h"
#include <sound/asound.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>

#define FRAME_RATE 8000
#define PERIOD_SIZE 4410
#define UTIMER_DEFAULT_ID -1
#define UTIMER_DEFAULT_FD -1
#define NANO 1000000000ULL
#define TICKS_COUNT 10
#define TICKS_RECORDING_DELTA 5
#define TIMER_OUTPUT_BUF_LEN 1024
#define TIMER_FREQ_SEC 1
#define RESULT_PREFIX_LEN strlen("Total ticks count: ")

enum timer_app_event {
	TIMER_APP_STARTED,
	TIMER_APP_RESULT,
	TIMER_NO_EVENT,
};

FIXTURE(timer_f) {
	struct snd_timer_uinfo *utimer_info;
};

FIXTURE_SETUP(timer_f) {
	int timer_dev_fd;

	if (geteuid())
		SKIP(return, "This test needs root to run!");

	self->utimer_info = calloc(1, sizeof(*self->utimer_info));
	ASSERT_NE(NULL, self->utimer_info);

	/* Resolution is the time the period of frames takes in nanoseconds */
	self->utimer_info->resolution = (NANO / FRAME_RATE * PERIOD_SIZE);

	timer_dev_fd = open("/dev/snd/timer", O_RDONLY);
	ASSERT_GE(timer_dev_fd, 0);

	ASSERT_EQ(ioctl(timer_dev_fd, SNDRV_TIMER_IOCTL_CREATE, self->utimer_info), 0);
	ASSERT_GE(self->utimer_info->fd, 0);

	close(timer_dev_fd);
}

FIXTURE_TEARDOWN(timer_f) {
	close(self->utimer_info->fd);
	free(self->utimer_info);
}

static void *ticking_func(void *data)
{
	int i;
	int *fd = (int *)data;

	for (i = 0; i < TICKS_COUNT; i++) {
		/* Well, trigger the timer! */
		ioctl(*fd, SNDRV_TIMER_IOCTL_TRIGGER, NULL);
		sleep(TIMER_FREQ_SEC);
	}

	return NULL;
}

static enum timer_app_event parse_timer_output(const char *s)
{
	if (strstr(s, "Timer has started"))
		return TIMER_APP_STARTED;
	if (strstr(s, "Total ticks count"))
		return TIMER_APP_RESULT;

	return TIMER_NO_EVENT;
}

static int parse_timer_result(const char *s)
{
	char *end;
	long d;

	d = strtol(s + RESULT_PREFIX_LEN, &end, 10);
	if (end == s + RESULT_PREFIX_LEN)
		return -1;

	return d;
}

/*
 * This test triggers the timer and counts ticks at the same time. The amount
 * of the timer trigger calls should be equal to the amount of ticks received.
 */
TEST_F(timer_f, utimer) {
	char command[64];
	pthread_t ticking_thread;
	int total_ticks = 0;
	FILE *rfp;
	char *buf = malloc(TIMER_OUTPUT_BUF_LEN);

	ASSERT_NE(buf, NULL);

	/* The timeout should be the ticks interval * count of ticks + some delta */
	sprintf(command, "./global-timer %d %d %d", SNDRV_TIMER_GLOBAL_UDRIVEN,
		self->utimer_info->id, TICKS_COUNT * TIMER_FREQ_SEC + TICKS_RECORDING_DELTA);

	rfp = popen(command, "r");
	while (fgets(buf, TIMER_OUTPUT_BUF_LEN, rfp)) {
		buf[TIMER_OUTPUT_BUF_LEN - 1] = 0;
		switch (parse_timer_output(buf)) {
		case TIMER_APP_STARTED:
			/* global-timer waits for timer to trigger, so start the ticking thread */
			pthread_create(&ticking_thread, NULL, ticking_func,
				       &self->utimer_info->fd);
			break;
		case TIMER_APP_RESULT:
			total_ticks = parse_timer_result(buf);
			break;
		case TIMER_NO_EVENT:
			break;
		}
	}
	pthread_join(ticking_thread, NULL);
	ASSERT_EQ(total_ticks, TICKS_COUNT);
	pclose(rfp);
}

TEST(wrong_timers_test) {
	int timer_dev_fd;
	int utimer_fd;
	size_t i;
	struct snd_timer_uinfo wrong_timer = {
		.resolution = 0,
		.id = UTIMER_DEFAULT_ID,
		.fd = UTIMER_DEFAULT_FD,
	};

	timer_dev_fd = open("/dev/snd/timer", O_RDONLY);
	ASSERT_GE(timer_dev_fd, 0);

	utimer_fd = ioctl(timer_dev_fd, SNDRV_TIMER_IOCTL_CREATE, &wrong_timer);
	ASSERT_LT(utimer_fd, 0);
	/* Check that id was not updated */
	ASSERT_EQ(wrong_timer.id, UTIMER_DEFAULT_ID);

	/* Test the NULL as an argument is processed correctly */
	ASSERT_LT(ioctl(timer_dev_fd, SNDRV_TIMER_IOCTL_CREATE, NULL), 0);

	close(timer_dev_fd);
}

TEST_HARNESS_MAIN
