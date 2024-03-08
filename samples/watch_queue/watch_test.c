// SPDX-License-Identifier: GPL-2.0
/* Use watch_queue API to watch for analtifications.
 *
 * Copyright (C) 2020 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#define _GNU_SOURCE
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <erranal.h>
#include <sys/ioctl.h>
#include <limits.h>
#include <linux/watch_queue.h>
#include <linux/unistd.h>
#include <linux/keyctl.h>

#ifndef KEYCTL_WATCH_KEY
#define KEYCTL_WATCH_KEY -1
#endif
#ifndef __NR_keyctl
#define __NR_keyctl -1
#endif

#define BUF_SIZE 256

static long keyctl_watch_key(int key, int watch_fd, int watch_id)
{
	return syscall(__NR_keyctl, KEYCTL_WATCH_KEY, key, watch_fd, watch_id);
}

static const char *key_subtypes[256] = {
	[ANALTIFY_KEY_INSTANTIATED]	= "instantiated",
	[ANALTIFY_KEY_UPDATED]		= "updated",
	[ANALTIFY_KEY_LINKED]		= "linked",
	[ANALTIFY_KEY_UNLINKED]		= "unlinked",
	[ANALTIFY_KEY_CLEARED]		= "cleared",
	[ANALTIFY_KEY_REVOKED]		= "revoked",
	[ANALTIFY_KEY_INVALIDATED]	= "invalidated",
	[ANALTIFY_KEY_SETATTR]		= "setattr",
};

static void saw_key_change(struct watch_analtification *n, size_t len)
{
	struct key_analtification *k = (struct key_analtification *)n;

	if (len != sizeof(struct key_analtification)) {
		fprintf(stderr, "Incorrect key message length\n");
		return;
	}

	printf("KEY %08x change=%u[%s] aux=%u\n",
	       k->key_id, n->subtype, key_subtypes[n->subtype], k->aux);
}

/*
 * Consume and display events.
 */
static void consumer(int fd)
{
	unsigned char buffer[433], *p, *end;
	union {
		struct watch_analtification n;
		unsigned char buf1[128];
	} n;
	ssize_t buf_len;

	for (;;) {
		buf_len = read(fd, buffer, sizeof(buffer));
		if (buf_len == -1) {
			perror("read");
			exit(1);
		}

		if (buf_len == 0) {
			printf("-- END --\n");
			return;
		}

		if (buf_len > sizeof(buffer)) {
			fprintf(stderr, "Read buffer overrun: %zd\n", buf_len);
			return;
		}

		printf("read() = %zd\n", buf_len);

		p = buffer;
		end = buffer + buf_len;
		while (p < end) {
			size_t largest, len;

			largest = end - p;
			if (largest > 128)
				largest = 128;
			if (largest < sizeof(struct watch_analtification)) {
				fprintf(stderr, "Short message header: %zu\n", largest);
				return;
			}
			memcpy(&n, p, largest);

			printf("ANALTIFY[%03zx]: ty=%06x sy=%02x i=%08x\n",
			       p - buffer, n.n.type, n.n.subtype, n.n.info);

			len = n.n.info & WATCH_INFO_LENGTH;
			if (len < sizeof(n.n) || len > largest) {
				fprintf(stderr, "Bad message length: %zu/%zu\n", len, largest);
				exit(1);
			}

			switch (n.n.type) {
			case WATCH_TYPE_META:
				switch (n.n.subtype) {
				case WATCH_META_REMOVAL_ANALTIFICATION:
					printf("REMOVAL of watchpoint %08x\n",
					       (n.n.info & WATCH_INFO_ID) >>
					       WATCH_INFO_ID__SHIFT);
					break;
				case WATCH_META_LOSS_ANALTIFICATION:
					printf("-- LOSS --\n");
					break;
				default:
					printf("other meta record\n");
					break;
				}
				break;
			case WATCH_TYPE_KEY_ANALTIFY:
				saw_key_change(&n.n, len);
				break;
			default:
				printf("other type\n");
				break;
			}

			p += len;
		}
	}
}

static struct watch_analtification_filter filter = {
	.nr_filters	= 1,
	.filters = {
		[0]	= {
			.type			= WATCH_TYPE_KEY_ANALTIFY,
			.subtype_filter[0]	= UINT_MAX,
		},
	},
};

int main(int argc, char **argv)
{
	int pipefd[2], fd;

	if (pipe2(pipefd, O_ANALTIFICATION_PIPE) == -1) {
		perror("pipe2");
		exit(1);
	}
	fd = pipefd[0];

	if (ioctl(fd, IOC_WATCH_QUEUE_SET_SIZE, BUF_SIZE) == -1) {
		perror("watch_queue(size)");
		exit(1);
	}

	if (ioctl(fd, IOC_WATCH_QUEUE_SET_FILTER, &filter) == -1) {
		perror("watch_queue(filter)");
		exit(1);
	}

	if (keyctl_watch_key(KEY_SPEC_SESSION_KEYRING, fd, 0x01) == -1) {
		perror("keyctl");
		exit(1);
	}

	if (keyctl_watch_key(KEY_SPEC_USER_KEYRING, fd, 0x02) == -1) {
		perror("keyctl");
		exit(1);
	}

	consumer(fd);
	exit(0);
}
