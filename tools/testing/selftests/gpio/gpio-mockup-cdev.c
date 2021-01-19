// SPDX-License-Identifier: GPL-2.0
/*
 * GPIO mockup cdev test helper
 *
 * Copyright (C) 2020 Kent Gibson
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/gpio.h>

#define CONSUMER	"gpio-mockup-cdev"

static int request_line_v1(int cfd, unsigned int offset,
			   uint32_t flags, unsigned int val)
{
	struct gpiohandle_request req;
	int ret;

	memset(&req, 0, sizeof(req));
	req.lines = 1;
	req.lineoffsets[0] = offset;
	req.flags = flags;
	strcpy(req.consumer_label, CONSUMER);
	if (flags & GPIOHANDLE_REQUEST_OUTPUT)
		req.default_values[0] = val;

	ret = ioctl(cfd, GPIO_GET_LINEHANDLE_IOCTL, &req);
	if (ret == -1)
		return -errno;
	return req.fd;
}

static int get_value_v1(int lfd)
{
	struct gpiohandle_data vals;
	int ret;

	memset(&vals, 0, sizeof(vals));
	ret = ioctl(lfd, GPIOHANDLE_GET_LINE_VALUES_IOCTL, &vals);
	if (ret == -1)
		return -errno;
	return vals.values[0];
}

static void usage(char *prog)
{
	printf("Usage: %s [-l] [-b <bias>] [-s <value>] [-u <uAPI>] <gpiochip> <offset>\n", prog);
	printf("        -b: set line bias to one of pull-down, pull-up, disabled\n");
	printf("               (default is to leave bias unchanged):\n");
	printf("        -l: set line active low (default is active high)\n");
	printf("        -s: set line value (default is to get line value)\n");
	exit(-1);
}

static int wait_signal(void)
{
	int sig;
	sigset_t wset;

	sigemptyset(&wset);
	sigaddset(&wset, SIGHUP);
	sigaddset(&wset, SIGINT);
	sigaddset(&wset, SIGTERM);
	sigwait(&wset, &sig);

	return sig;
}

int main(int argc, char *argv[])
{
	char *chip;
	int opt, ret, cfd, lfd;
	unsigned int offset, val;
	uint32_t flags_v1;

	ret = 0;
	flags_v1 = GPIOHANDLE_REQUEST_INPUT;

	while ((opt = getopt(argc, argv, "lb:s:u:")) != -1) {
		switch (opt) {
		case 'l':
			flags_v1 |= GPIOHANDLE_REQUEST_ACTIVE_LOW;
			break;
		case 'b':
			if (strcmp("pull-up", optarg) == 0)
				flags_v1 |= GPIOHANDLE_REQUEST_BIAS_PULL_UP;
			else if (strcmp("pull-down", optarg) == 0)
				flags_v1 |= GPIOHANDLE_REQUEST_BIAS_PULL_DOWN;
			else if (strcmp("disabled", optarg) == 0)
				flags_v1 |= GPIOHANDLE_REQUEST_BIAS_DISABLE;
			break;
		case 's':
			val = atoi(optarg);
			flags_v1 &= ~GPIOHANDLE_REQUEST_INPUT;
			flags_v1 |= GPIOHANDLE_REQUEST_OUTPUT;
			break;
		default:
			usage(argv[0]);
		}
	}

	if (argc < optind + 2)
		usage(argv[0]);

	chip = argv[optind];
	offset = atoi(argv[optind + 1]);

	cfd = open(chip, 0);
	if (cfd == -1) {
		fprintf(stderr, "Failed to open %s: %s\n", chip, strerror(errno));
		return -errno;
	}

	lfd = request_line_v1(cfd, offset, flags_v1, val);

	close(cfd);

	if (lfd < 0) {
		fprintf(stderr, "Failed to request %s:%d: %s\n", chip, offset, strerror(-lfd));
		return lfd;
	}

	if (flags_v1 & GPIOHANDLE_REQUEST_OUTPUT)
		wait_signal();
	else
		ret = get_value_v1(lfd);

	close(lfd);

	return ret;
}
