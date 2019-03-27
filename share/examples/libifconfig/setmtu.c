/*
 * Copyright (c) 2016-2017, Marie Helene Kvello-Aune
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * thislist of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation and/or
 * other materials provided with the distribution.
 *
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <err.h>
#include <errno.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libifconfig.h>


int
main(int argc, char *argv[])
{
	char *ifname, *ptr;
	int mtu;
	ifconfig_handle_t *lifh;

	if (argc != 3) {
		errx(EINVAL, "Invalid number of arguments."
		    " First argument should be interface name, second argument"
		    " should be the MTU to set.");
	}

	/* We have a static number of arguments. Therefore we can do it simple. */
	ifname = strdup(argv[1]);
	mtu = (int)strtol(argv[2], &ptr, 10);

	printf("Interface name: %s\n", ifname);
	printf("New MTU: %d", mtu);

	lifh = ifconfig_open();
	if (lifh == NULL) {
		errx(ENOMEM, "Failed to open libifconfig handle.");
		return (-1);
	}

	if (ifconfig_set_mtu(lifh, ifname, mtu) == 0) {
		printf("Successfully changed MTU of %s to %d\n", ifname, mtu);
		ifconfig_close(lifh);
		lifh = NULL;
		free(ifname);
		return (0);
	}

	switch (ifconfig_err_errtype(lifh)) {
	case SOCKET:
		warnx("couldn't create socket. This shouldn't happen.\n");
		break;
	case IOCTL:
		if (ifconfig_err_ioctlreq(lifh) == SIOCSIFMTU) {
			warnx("Failed to set MTU (SIOCSIFMTU)\n");
		} else {
			warnx(
				"Failed to set MTU due to error in unexpected ioctl() call %lu. Error code: %i.\n",
				ifconfig_err_ioctlreq(lifh),
				ifconfig_err_errno(lifh));
		}
		break;
	default:
		warnx(
			"Should basically never end up here in this example.\n");
		break;
	}

	ifconfig_close(lifh);
	lifh = NULL;
	free(ifname);
	return (-1);
}
