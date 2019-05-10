// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <sys/uio.h>
#include <errno.h>
#include <stdio.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include "../../include/uapi/linux/bpf.h"
#include <asm/unistd.h>
#include "msgfmt.h"

int debug_fd;

static int handle_get_cmd(struct mbox_request *cmd)
{
	switch (cmd->cmd) {
	case 0:
		return 0;
	default:
		break;
	}
	return -ENOPROTOOPT;
}

static int handle_set_cmd(struct mbox_request *cmd)
{
	return -ENOPROTOOPT;
}

static void loop(void)
{
	while (1) {
		struct mbox_request req;
		struct mbox_reply reply;
		int n;

		n = read(0, &req, sizeof(req));
		if (n != sizeof(req)) {
			dprintf(debug_fd, "invalid request %d\n", n);
			return;
		}

		reply.status = req.is_set ?
			handle_set_cmd(&req) :
			handle_get_cmd(&req);

		n = write(1, &reply, sizeof(reply));
		if (n != sizeof(reply)) {
			dprintf(debug_fd, "reply failed %d\n", n);
			return;
		}
	}
}

int main(void)
{
	debug_fd = open("/dev/console", 00000002);
	dprintf(debug_fd, "Started bpfilter\n");
	loop();
	close(debug_fd);
	return 0;
}
