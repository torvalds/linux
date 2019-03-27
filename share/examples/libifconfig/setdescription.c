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

#include <arpa/inet.h>
#include <net/if.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libifconfig.h>


int
main(int argc, char *argv[])
{
	char *ifname, *ifdescr, *curdescr;
	ifconfig_handle_t *lifh;

	if (argc != 3) {
		errx(EINVAL, "Invalid number of arguments."
		    " First argument should be interface name, second argument"
		    " should be the description to set.");
	}

	/* We have a static number of arguments. Therefore we can do it simple. */
	ifname = strdup(argv[1]);
	ifdescr = strdup(argv[2]);
	curdescr = NULL;

	printf("Interface name: %s\n", ifname);

	lifh = ifconfig_open();
	if (lifh == NULL) {
		errx(ENOMEM, "Failed to open libifconfig handle.");
		return (-1);
	}

	if (ifconfig_get_description(lifh, ifname, &curdescr) == 0) {
		printf("Old description: %s\n", curdescr);
	}

	printf("New description: %s\n\n", ifdescr);

	if (ifconfig_set_description(lifh, ifname, ifdescr) == 0) {
		printf("New description successfully set.\n");
	} else {
		switch (ifconfig_err_errtype(lifh)) {
		case SOCKET:
			err(ifconfig_err_errno(lifh), "Socket error");
			break;
		case IOCTL:
			err(ifconfig_err_errno(
				    lifh), "IOCTL(%lu) error",
			    ifconfig_err_ioctlreq(lifh));
			break;
		default:
			err(ifconfig_err_errno(lifh), "Other error");
			break;
		}
	}

	free(ifname);
	free(ifdescr);
	free(curdescr);
	ifname = NULL;
	ifdescr = NULL;
	curdescr = NULL;

	ifconfig_close(lifh);
	return (0);
}
