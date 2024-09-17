// SPDX-License-Identifier: GPL-2.0
/*
 * This tool is used by the utimer test, and it allows us to
 * count the ticks of a global timer in a certain time frame
 * (which is set by `timeout` parameter).
 *
 * Author: Ivan Orlov <ivan.orlov0322@gmail.com>
 */
#include <stdio.h>
#include <stdlib.h>
#include <alsa/asoundlib.h>
#include <time.h>

static int ticked;
static void async_callback(snd_async_handler_t *ahandler)
{
	ticked++;
}

static char timer_name[64];
static void bind_to_timer(int device, int subdevice, int timeout)
{
	snd_timer_t *handle;
	snd_timer_params_t *params;
	snd_async_handler_t *ahandler;

	time_t end;

	sprintf(timer_name, "hw:CLASS=%d,SCLASS=%d,DEV=%d,SUBDEV=%d",
		SND_TIMER_CLASS_GLOBAL, SND_TIMER_SCLASS_NONE,
		device, subdevice);

	snd_timer_params_alloca(&params);

	if (snd_timer_open(&handle, timer_name, SND_TIMER_OPEN_NONBLOCK) < 0) {
		perror("Can't open the timer");
		exit(EXIT_FAILURE);
	}

	snd_timer_params_set_auto_start(params, 1);
	snd_timer_params_set_ticks(params, 1);
	if (snd_timer_params(handle, params) < 0) {
		perror("Can't set timer params");
		exit(EXIT_FAILURE);
	}

	if (snd_async_add_timer_handler(&ahandler, handle, async_callback, NULL) < 0) {
		perror("Can't create a handler");
		exit(EXIT_FAILURE);
	}
	end = time(NULL) + timeout;
	if (snd_timer_start(handle) < 0) {
		perror("Failed to start the timer");
		exit(EXIT_FAILURE);
	}
	printf("Timer has started\n");
	while (time(NULL) <= end) {
		/*
		 * Waiting for the timeout to elapse. Can't use sleep here, as it gets
		 * constantly interrupted by the signal from the timer (SIGIO)
		 */
	}
	snd_timer_stop(handle);
	snd_timer_close(handle);
}

int main(int argc, char *argv[])
{
	int device, subdevice, timeout;

	if (argc < 4) {
		perror("Usage: %s <device> <subdevice> <timeout>");
		return EXIT_FAILURE;
	}

	setlinebuf(stdout);

	device = atoi(argv[1]);
	subdevice = atoi(argv[2]);
	timeout = atoi(argv[3]);

	bind_to_timer(device, subdevice, timeout);

	printf("Total ticks count: %d\n", ticked);

	return EXIT_SUCCESS;
}
