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
	char *ifname;
	ifconfig_handle_t *lifh;

	if (argc != 2) {
		errx(EINVAL, "Invalid number of arguments."
		    " Only one argument is accepted, and it should be the name"
		    " of the interface to be destroyed.");
	}

	/* We have a static number of arguments. Therefore we can do it simple. */
	ifname = strdup(argv[1]);

	printf("Interface name: %s\n", ifname);

	lifh = ifconfig_open();
	if (lifh == NULL) {
		errx(ENOMEM, "Failed to open libifconfig handle.");
		return (-1);
	}

	if (ifconfig_destroy_interface(lifh, ifname) == 0) {
		printf("Successfully destroyed interface '%s'.", ifname);
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
		if (ifconfig_err_ioctlreq(lifh) == SIOCIFDESTROY) {
			warnx(
				"Failed to destroy interface (SIOCIFDESTROY)\n");
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
