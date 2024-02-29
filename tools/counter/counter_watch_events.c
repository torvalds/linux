// SPDX-License-Identifier: GPL-2.0-only
/*
 * Counter Watch Events - Test various counter watch events in a userspace application
 *
 * Copyright (C) STMicroelectronics 2023 - All Rights Reserved
 * Author: Fabrice Gasnier <fabrice.gasnier@foss.st.com>.
 */

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <linux/counter.h>
#include <linux/kernel.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static struct counter_watch simple_watch[] = {
	{
		/* Component data: Count 0 count */
		.component.type = COUNTER_COMPONENT_COUNT,
		.component.scope = COUNTER_SCOPE_COUNT,
		.component.parent = 0,
		/* Event type: overflow or underflow */
		.event = COUNTER_EVENT_OVERFLOW_UNDERFLOW,
		/* Device event channel 0 */
		.channel = 0,
	},
};

static const char * const counter_event_type_name[] = {
	"COUNTER_EVENT_OVERFLOW",
	"COUNTER_EVENT_UNDERFLOW",
	"COUNTER_EVENT_OVERFLOW_UNDERFLOW",
	"COUNTER_EVENT_THRESHOLD",
	"COUNTER_EVENT_INDEX",
	"COUNTER_EVENT_CHANGE_OF_STATE",
	"COUNTER_EVENT_CAPTURE",
};

static const char * const counter_component_type_name[] = {
	"COUNTER_COMPONENT_NONE",
	"COUNTER_COMPONENT_SIGNAL",
	"COUNTER_COMPONENT_COUNT",
	"COUNTER_COMPONENT_FUNCTION",
	"COUNTER_COMPONENT_SYNAPSE_ACTION",
	"COUNTER_COMPONENT_EXTENSION",
};

static const char * const counter_scope_name[] = {
	"COUNTER_SCOPE_DEVICE",
	"COUNTER_SCOPE_SIGNAL",
	"COUNTER_SCOPE_COUNT",
};

static void print_watch(struct counter_watch *watch, int nwatch)
{
	int i;

	/* prints the watch array in C-like structure */
	printf("watch[%d] = {\n", nwatch);
	for (i = 0; i < nwatch; i++) {
		printf(" [%d] =\t{\n"
		       "\t\t.component.type = %s\n"
		       "\t\t.component.scope = %s\n"
		       "\t\t.component.parent = %d\n"
		       "\t\t.component.id = %d\n"
		       "\t\t.event = %s\n"
		       "\t\t.channel = %d\n"
		       "\t},\n",
		       i,
		       counter_component_type_name[watch[i].component.type],
		       counter_scope_name[watch[i].component.scope],
		       watch[i].component.parent,
		       watch[i].component.id,
		       counter_event_type_name[watch[i].event],
		       watch[i].channel);
	}
	printf("};\n");
}

static void print_usage(void)
{
	fprintf(stderr, "Usage:\n\n"
		"counter_watch_events [options] [-w <watchoptions>]\n"
		"counter_watch_events [options] [-w <watch1 options>] [-w <watch2 options>]...\n"
		"\n"
		"When no --watch option has been provided, simple watch example is used:\n"
		"counter_watch_events [options] -w comp_count,scope_count,evt_ovf_udf\n"
		"\n"
		"Test various watch events for given counter device.\n"
		"\n"
		"Options:\n"
		"  -d, --debug                Prints debug information\n"
		"  -h, --help                 Prints usage\n"
		"  -n, --device-num <n>       Use /dev/counter<n> [default: /dev/counter0]\n"
		"  -l, --loop <n>             Loop for <n> events [default: 0 (forever)]\n"
		"  -w, --watch <watchoptions> comma-separated list of watch options\n"
		"\n"
		"Watch options:\n"
		"  scope_device               (COUNTER_SCOPE_DEVICE) [default: scope_device]\n"
		"  scope_signal               (COUNTER_SCOPE_SIGNAL)\n"
		"  scope_count                (COUNTER_SCOPE_COUNT)\n"
		"\n"
		"  comp_none                  (COUNTER_COMPONENT_NONE) [default: comp_none]\n"
		"  comp_signal                (COUNTER_COMPONENT_SIGNAL)\n"
		"  comp_count                 (COUNTER_COMPONENT_COUNT)\n"
		"  comp_function              (COUNTER_COMPONENT_FUNCTION)\n"
		"  comp_synapse_action        (COUNTER_COMPONENT_SYNAPSE_ACTION)\n"
		"  comp_extension             (COUNTER_COMPONENT_EXTENSION)\n"
		"\n"
		"  evt_ovf                    (COUNTER_EVENT_OVERFLOW) [default: evt_ovf]\n"
		"  evt_udf                    (COUNTER_EVENT_UNDERFLOW)\n"
		"  evt_ovf_udf                (COUNTER_EVENT_OVERFLOW_UNDERFLOW)\n"
		"  evt_threshold              (COUNTER_EVENT_THRESHOLD)\n"
		"  evt_index                  (COUNTER_EVENT_INDEX)\n"
		"  evt_change_of_state        (COUNTER_EVENT_CHANGE_OF_STATE)\n"
		"  evt_capture                (COUNTER_EVENT_CAPTURE)\n"
		"\n"
		"  chan=<n>                   channel <n> for this watch [default: 0]\n"
		"  id=<n>                     component id <n> for this watch [default: 0]\n"
		"  parent=<n>                 component parent <n> for this watch [default: 0]\n"
		"\n"
		"Example with two watched events:\n\n"
		"counter_watch_events -d \\\n"
		"\t-w comp_count,scope_count,evt_ovf_udf \\\n"
		"\t-w comp_extension,scope_count,evt_capture,id=7,chan=3\n"
		);
}

static const struct option longopts[] = {
	{ "debug",		no_argument,       0, 'd' },
	{ "help",		no_argument,       0, 'h' },
	{ "device-num",		required_argument, 0, 'n' },
	{ "loop",		required_argument, 0, 'l' },
	{ "watch",		required_argument, 0, 'w' },
	{ },
};

/* counter watch subopts */
enum {
	WATCH_SCOPE_DEVICE,
	WATCH_SCOPE_SIGNAL,
	WATCH_SCOPE_COUNT,
	WATCH_COMPONENT_NONE,
	WATCH_COMPONENT_SIGNAL,
	WATCH_COMPONENT_COUNT,
	WATCH_COMPONENT_FUNCTION,
	WATCH_COMPONENT_SYNAPSE_ACTION,
	WATCH_COMPONENT_EXTENSION,
	WATCH_EVENT_OVERFLOW,
	WATCH_EVENT_UNDERFLOW,
	WATCH_EVENT_OVERFLOW_UNDERFLOW,
	WATCH_EVENT_THRESHOLD,
	WATCH_EVENT_INDEX,
	WATCH_EVENT_CHANGE_OF_STATE,
	WATCH_EVENT_CAPTURE,
	WATCH_CHANNEL,
	WATCH_ID,
	WATCH_PARENT,
	WATCH_SUBOPTS_MAX,
};

static char * const counter_watch_subopts[WATCH_SUBOPTS_MAX + 1] = {
	/* component.scope */
	[WATCH_SCOPE_DEVICE] = "scope_device",
	[WATCH_SCOPE_SIGNAL] = "scope_signal",
	[WATCH_SCOPE_COUNT] = "scope_count",
	/* component.type */
	[WATCH_COMPONENT_NONE] = "comp_none",
	[WATCH_COMPONENT_SIGNAL] = "comp_signal",
	[WATCH_COMPONENT_COUNT] = "comp_count",
	[WATCH_COMPONENT_FUNCTION] = "comp_function",
	[WATCH_COMPONENT_SYNAPSE_ACTION] = "comp_synapse_action",
	[WATCH_COMPONENT_EXTENSION] = "comp_extension",
	/* event */
	[WATCH_EVENT_OVERFLOW] = "evt_ovf",
	[WATCH_EVENT_UNDERFLOW] = "evt_udf",
	[WATCH_EVENT_OVERFLOW_UNDERFLOW] = "evt_ovf_udf",
	[WATCH_EVENT_THRESHOLD] = "evt_threshold",
	[WATCH_EVENT_INDEX] = "evt_index",
	[WATCH_EVENT_CHANGE_OF_STATE] = "evt_change_of_state",
	[WATCH_EVENT_CAPTURE] = "evt_capture",
	/* channel, id, parent */
	[WATCH_CHANNEL] = "chan",
	[WATCH_ID] = "id",
	[WATCH_PARENT] = "parent",
	/* Empty entry ends the opts array */
	NULL
};

int main(int argc, char **argv)
{
	int c, fd, i, ret, rc = 0, debug = 0, loop = 0, dev_num = 0, nwatch = 0;
	struct counter_event event_data;
	char *device_name = NULL, *subopts, *value;
	struct counter_watch *watches;

	/*
	 * 1st pass:
	 * - list watch events number to allocate the watch array.
	 * - parse normal options (other than watch options)
	 */
	while ((c = getopt_long(argc, argv, "dhn:l:w:", longopts, NULL)) != -1) {
		switch (c) {
		case 'd':
			debug = 1;
			break;
		case 'h':
			print_usage();
			return EXIT_SUCCESS;
		case 'n':
			dev_num = strtoul(optarg, NULL, 10);
			if (errno) {
				perror("strtol failed: --device-num <n>\n");
				return EXIT_FAILURE;
			}
			break;
		case 'l':
			loop = strtol(optarg, NULL, 10);
			if (errno) {
				perror("strtol failed: --loop <n>\n");
				return EXIT_FAILURE;
			}
			break;
		case 'w':
			nwatch++;
			break;
		default:
			return EXIT_FAILURE;
		}
	}

	if (nwatch) {
		watches = calloc(nwatch, sizeof(*watches));
		if (!watches) {
			perror("Error allocating watches\n");
			return EXIT_FAILURE;
		}
	} else {
		/* default to simple watch example */
		watches = simple_watch;
		nwatch = ARRAY_SIZE(simple_watch);
	}

	/* 2nd pass: parse watch sub-options to fill in watch array */
	optind = 1;
	i = 0;
	while ((c = getopt_long(argc, argv, "dhn:l:w:", longopts, NULL)) != -1) {
		switch (c) {
		case 'w':
			subopts = optarg;
			while (*subopts != '\0') {
				ret = getsubopt(&subopts, counter_watch_subopts, &value);
				switch (ret) {
				case WATCH_SCOPE_DEVICE:
				case WATCH_SCOPE_SIGNAL:
				case WATCH_SCOPE_COUNT:
					/* match with counter_scope */
					watches[i].component.scope = ret;
					break;
				case WATCH_COMPONENT_NONE:
				case WATCH_COMPONENT_SIGNAL:
				case WATCH_COMPONENT_COUNT:
				case WATCH_COMPONENT_FUNCTION:
				case WATCH_COMPONENT_SYNAPSE_ACTION:
				case WATCH_COMPONENT_EXTENSION:
					/* match counter_component_type: subtract enum value */
					ret -= WATCH_COMPONENT_NONE;
					watches[i].component.type = ret;
					break;
				case WATCH_EVENT_OVERFLOW:
				case WATCH_EVENT_UNDERFLOW:
				case WATCH_EVENT_OVERFLOW_UNDERFLOW:
				case WATCH_EVENT_THRESHOLD:
				case WATCH_EVENT_INDEX:
				case WATCH_EVENT_CHANGE_OF_STATE:
				case WATCH_EVENT_CAPTURE:
					/* match counter_event_type: subtract enum value */
					ret -= WATCH_EVENT_OVERFLOW;
					watches[i].event = ret;
					break;
				case WATCH_CHANNEL:
					if (!value) {
						fprintf(stderr, "Invalid chan=<number>\n");
						rc = EXIT_FAILURE;
						goto err_free_watches;
					}
					watches[i].channel = strtoul(value, NULL, 10);
					if (errno) {
						perror("strtoul failed: chan=<number>\n");
						rc = EXIT_FAILURE;
						goto err_free_watches;
					}
					break;
				case WATCH_ID:
					if (!value) {
						fprintf(stderr, "Invalid id=<number>\n");
						rc = EXIT_FAILURE;
						goto err_free_watches;
					}
					watches[i].component.id = strtoul(value, NULL, 10);
					if (errno) {
						perror("strtoul failed: id=<number>\n");
						rc = EXIT_FAILURE;
						goto err_free_watches;
					}
					break;
				case WATCH_PARENT:
					if (!value) {
						fprintf(stderr, "Invalid parent=<number>\n");
						rc = EXIT_FAILURE;
						goto err_free_watches;
					}
					watches[i].component.parent = strtoul(value, NULL, 10);
					if (errno) {
						perror("strtoul failed: parent=<number>\n");
						rc = EXIT_FAILURE;
						goto err_free_watches;
					}
					break;
				default:
					fprintf(stderr, "Unknown suboption '%s'\n", value);
					rc = EXIT_FAILURE;
					goto err_free_watches;
				}
			}
			i++;
			break;
		}
	}

	if (debug)
		print_watch(watches, nwatch);

	ret = asprintf(&device_name, "/dev/counter%d", dev_num);
	if (ret < 0) {
		fprintf(stderr, "asprintf failed\n");
		rc = EXIT_FAILURE;
		goto err_free_watches;
	}

	if (debug)
		printf("Opening %s\n", device_name);

	fd = open(device_name, O_RDWR);
	if (fd == -1) {
		fprintf(stderr, "Unable to open %s: %s\n", device_name, strerror(errno));
		free(device_name);
		rc = EXIT_FAILURE;
		goto err_free_watches;
	}
	free(device_name);

	for (i = 0; i < nwatch; i++) {
		ret = ioctl(fd, COUNTER_ADD_WATCH_IOCTL, watches + i);
		if (ret == -1) {
			fprintf(stderr, "Error adding watches[%d]: %s\n", i,
				strerror(errno));
			rc = EXIT_FAILURE;
			goto err_close;
		}
	}

	ret = ioctl(fd, COUNTER_ENABLE_EVENTS_IOCTL);
	if (ret == -1) {
		perror("Error enabling events");
		rc = EXIT_FAILURE;
		goto err_close;
	}

	for (i = 0; loop <= 0 || i < loop; i++) {
		ret = read(fd, &event_data, sizeof(event_data));
		if (ret == -1) {
			perror("Failed to read event data");
			rc = EXIT_FAILURE;
			goto err_close;
		}

		if (ret != sizeof(event_data)) {
			fprintf(stderr, "Failed to read event data (got: %d)\n", ret);
			rc = EXIT_FAILURE;
			goto err_close;
		}

		printf("Timestamp: %llu\tData: %llu\t event: %s\tch: %d\n",
		       event_data.timestamp, event_data.value,
		       counter_event_type_name[event_data.watch.event],
		       event_data.watch.channel);

		if (event_data.status) {
			fprintf(stderr, "Error %d: %s\n", event_data.status,
				strerror(event_data.status));
		}
	}

err_close:
	close(fd);
err_free_watches:
	if (watches != simple_watch)
		free(watches);

	return rc;
}
