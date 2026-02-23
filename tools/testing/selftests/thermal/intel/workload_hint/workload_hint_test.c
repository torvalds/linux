// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>

#define WORKLOAD_NOTIFICATION_DELAY_ATTRIBUTE "/sys/bus/pci/devices/0000:00:04.0/workload_hint/notification_delay_ms"
#define WORKLOAD_ENABLE_ATTRIBUTE "/sys/bus/pci/devices/0000:00:04.0/workload_hint/workload_hint_enable"
#define WORKLOAD_SLOW_ENABLE_ATTRIBUTE "/sys/bus/pci/devices/0000:00:04.0/workload_hint/workload_slow_hint_enable"
#define WORKLOAD_TYPE_INDEX_ATTRIBUTE  "/sys/bus/pci/devices/0000:00:04.0/workload_hint/workload_type_index"

static const char * const workload_types[] = {
	"idle",
	"battery_life",
	"sustained",
	"bursty",
	NULL
};

static int wlt_slow;
static char *wlt_enable_attr;

#define WORKLOAD_TYPE_MAX_INDEX	3

void workload_hint_exit(int signum)
{
	int fd;

	/* Disable feature via sysfs knob */

	fd = open(wlt_enable_attr, O_RDWR);
	if (fd < 0) {
		perror("Unable to open workload type feature enable file");
		exit(1);
	}

	if (write(fd, "0\n", 2) < 0) {
		perror("Can't disable workload hints");
		exit(1);
	}

	printf("Disabled workload type prediction\n");

	close(fd);
}

static void update_delay(char *delay_str)
{
	int fd;

	printf("Setting notification delay in ms to %s\n", delay_str);

	fd = open(WORKLOAD_NOTIFICATION_DELAY_ATTRIBUTE, O_RDWR);
	if (fd < 0) {
		perror("Unable to open workload notification delay");
		exit(1);
	}

	if (write(fd, delay_str, strlen(delay_str)) < 0) {
		perror("Can't set delay");
		exit(1);
	}

	close(fd);
}

int main(int argc, char **argv)
{
	struct pollfd ufd;
	char index_str[4];
	int fd, ret, index;
	char delay_str[64];
	int delay = 0;

	printf("Usage: workload_hint_test [notification delay in milli seconds][slow]\n");

	if (argc > 1) {
		int i;

		for (i = 1; i < argc; ++i) {
			if (!strcmp(argv[i], "slow")) {
				wlt_slow = 1;
				continue;
			}

			ret = sscanf(argv[1], "%d", &delay);
			if (ret < 0) {
				printf("Invalid delay\n");
				exit(1);
			}

			sprintf(delay_str, "%s\n", argv[1]);
			update_delay(delay_str);
		}
	}

	if (signal(SIGINT, workload_hint_exit) == SIG_IGN)
		signal(SIGINT, SIG_IGN);
	if (signal(SIGHUP, workload_hint_exit) == SIG_IGN)
		signal(SIGHUP, SIG_IGN);
	if (signal(SIGTERM, workload_hint_exit) == SIG_IGN)
		signal(SIGTERM, SIG_IGN);

	if (wlt_slow)
		wlt_enable_attr = WORKLOAD_SLOW_ENABLE_ATTRIBUTE;
	else
		wlt_enable_attr = WORKLOAD_ENABLE_ATTRIBUTE;

	/* Enable feature via sysfs knob */
	fd = open(wlt_enable_attr, O_RDWR);
	if (fd < 0) {
		perror("Unable to open workload type feature enable file");
		exit(1);
	}

	if (write(fd, "1\n", 2) < 0) {
		perror("Can't enable workload hints");
		exit(1);
	}

	close(fd);

	printf("Enabled workload type prediction\n");

	while (1) {
		fd = open(WORKLOAD_TYPE_INDEX_ATTRIBUTE, O_RDONLY);
		if (fd < 0) {
			perror("Unable to open workload type file");
			exit(1);
		}

		if ((lseek(fd, 0L, SEEK_SET)) < 0) {
			fprintf(stderr, "Failed to set pointer to beginning\n");
			exit(1);
		}

		if (read(fd, index_str, sizeof(index_str)) < 0) {
			fprintf(stderr, "Failed to read from:%s\n",
			WORKLOAD_TYPE_INDEX_ATTRIBUTE);
			exit(1);
		}

		ufd.fd = fd;
		ufd.events = POLLPRI;

		ret = poll(&ufd, 1, -1);
		if (ret < 0) {
			perror("poll error");
			exit(1);
		} else if (ret == 0) {
			printf("Poll Timeout\n");
		} else {
			if ((lseek(fd, 0L, SEEK_SET)) < 0) {
				fprintf(stderr, "Failed to set pointer to beginning\n");
				exit(1);
			}

			if (read(fd, index_str, sizeof(index_str)) < 0)
				exit(0);

			ret = sscanf(index_str, "%d", &index);
			if (ret < 0)
				break;

			if (wlt_slow) {
				if (index & 0x10)
					printf("workload type slow:%s\n", "power");
				else
					printf("workload type slow:%s\n", "performance");
			}

			index &= 0x0f;
			if (index > WORKLOAD_TYPE_MAX_INDEX)
				printf("Invalid workload type index\n");
			else
				printf("workload type:%s\n", workload_types[index]);
		}

		close(fd);
	}
}
