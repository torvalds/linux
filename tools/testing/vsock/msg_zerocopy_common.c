// SPDX-License-Identifier: GPL-2.0-only
/* Some common code for MSG_ZEROCOPY logic
 *
 * Copyright (C) 2023 SberDevices.
 *
 * Author: Arseniy Krasnov <avkrasnov@salutedevices.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <linux/errqueue.h>

#include "msg_zerocopy_common.h"

void enable_so_zerocopy(int fd)
{
	int val = 1;

	if (setsockopt(fd, SOL_SOCKET, SO_ZEROCOPY, &val, sizeof(val))) {
		perror("setsockopt");
		exit(EXIT_FAILURE);
	}
}

void vsock_recv_completion(int fd, const bool *zerocopied)
{
	struct sock_extended_err *serr;
	struct msghdr msg = { 0 };
	char cmsg_data[128];
	struct cmsghdr *cm;
	ssize_t res;

	msg.msg_control = cmsg_data;
	msg.msg_controllen = sizeof(cmsg_data);

	res = recvmsg(fd, &msg, MSG_ERRQUEUE);
	if (res) {
		fprintf(stderr, "failed to read error queue: %zi\n", res);
		exit(EXIT_FAILURE);
	}

	cm = CMSG_FIRSTHDR(&msg);
	if (!cm) {
		fprintf(stderr, "cmsg: no cmsg\n");
		exit(EXIT_FAILURE);
	}

	if (cm->cmsg_level != SOL_VSOCK) {
		fprintf(stderr, "cmsg: unexpected 'cmsg_level'\n");
		exit(EXIT_FAILURE);
	}

	if (cm->cmsg_type != VSOCK_RECVERR) {
		fprintf(stderr, "cmsg: unexpected 'cmsg_type'\n");
		exit(EXIT_FAILURE);
	}

	serr = (void *)CMSG_DATA(cm);
	if (serr->ee_origin != SO_EE_ORIGIN_ZEROCOPY) {
		fprintf(stderr, "serr: wrong origin: %u\n", serr->ee_origin);
		exit(EXIT_FAILURE);
	}

	if (serr->ee_errno) {
		fprintf(stderr, "serr: wrong error code: %u\n", serr->ee_errno);
		exit(EXIT_FAILURE);
	}

	/* This flag is used for tests, to check that transmission was
	 * performed as expected: zerocopy or fallback to copy. If NULL
	 * - don't care.
	 */
	if (!zerocopied)
		return;

	if (*zerocopied && (serr->ee_code & SO_EE_CODE_ZEROCOPY_COPIED)) {
		fprintf(stderr, "serr: was copy instead of zerocopy\n");
		exit(EXIT_FAILURE);
	}

	if (!*zerocopied && !(serr->ee_code & SO_EE_CODE_ZEROCOPY_COPIED)) {
		fprintf(stderr, "serr: was zerocopy instead of copy\n");
		exit(EXIT_FAILURE);
	}
}
